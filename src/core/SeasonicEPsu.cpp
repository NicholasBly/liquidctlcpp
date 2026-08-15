// LiquidCam - SeasonicEPsu.cpp
#include "SeasonicEPsu.h"
#include "Pmbus.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace lc {
namespace {

constexpr uint8_t kHidWrite     = 0xAC;   // "write" transaction, first byte of the report
constexpr uint8_t kHidRead      = 0xAD;   // "read" transaction, first byte of the report
constexpr uint8_t kHidAck       = 0xAA;   // first byte of a well-formed response
constexpr uint8_t kSlaveAddress = 0x60;   // PMBus address of the PSU controller
constexpr int     kAttempts     = 3;
constexpr long long kMinGapUs   = 2500;   // the controller drops back-to-back commands

struct KnownPsu { uint16_t pid; const wchar_t* name; };
constexpr KnownPsu kKnownPsus[] = {
    { 0x2500, L"NZXT E850" },
    { 0x5912, L"NZXT E650" },
    { 0x5911, L"NZXT E500" },
};

long long nowMicros()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

} // namespace

bool SeasonicEPsu::open()
{
    for (const auto& known : kKnownPsus) {
        for (const auto& info : hidEnumerate(kVendorId, known.pid)) {
            if (dev_.open(info)) {
                // liquidctl builds a 1 + 64 byte packet with a zero in front:
                // this interface has no numbered reports.
                dev_.setUsesReportIds(false);
                model_ = known.name;
                return true;
            }
            lastError_ = dev_.lastError();
        }
    }
    return false;
}

void SeasonicEPsu::pace()
{
    const long long elapsed = nowMicros() - lastTxUs_;
    if (elapsed < kMinGapUs)
        std::this_thread::sleep_for(std::chrono::microseconds(kMinGapUs - elapsed));
}

bool SeasonicEPsu::transact(const uint8_t* msg, size_t len, uint8_t* response)
{
    pace();

    // Drop anything the driver still holds before asking a new question. The
    // controller answers every request, so a response that was never collected
    // - a read that timed out, or an attempt whose reply failed validation -
    // stays queued and is handed to the *next* read instead. One stale report
    // desynchronises the whole sweep: temperature returns the fan reading, the
    // fan returns something else, and every rail read fails its length check.
    // Each retry then adds another orphan, so the backlog only grows.
    dev_.flushInput();

    const bool ok = dev_.writeMessage(msg, len);
    lastTxUs_ = nowMicros();
    if (!ok) {
        lastError_ = dev_.lastError();
        return false;
    }

    const int got = dev_.readReport(response, kReadLength, 250);
    if (got <= 0) {
        lastError_ = (got == 0) ? "PSU response timed out" : dev_.lastError();
        return false;
    }

    // If the interface turns out to use unnumbered reports, Windows hands us a
    // leading 0x00 placeholder. Slide past it so the offsets below stay valid.
    size_t valid = static_cast<size_t>(got);
    if (response[0] == 0x00 && got > 1 && response[1] == kHidAck) {
        std::memmove(response, response + 1, valid - 1);
        --valid;
    }

    // Keep only what actually arrived; a short reply must not read past itself.
    const size_t keep = (valid < sizeof(lastReply_)) ? valid : sizeof(lastReply_);
    std::memset(lastReply_, 0, sizeof(lastReply_));
    std::memcpy(lastReply_, response, keep);
    return true;
}

// Puts the head of the last reply in the error text, so a protocol mismatch
// shows up in the activity log as evidence instead of a shrug.
void SeasonicEPsu::noteBadReply(const char* what)
{
    char text[96];
    std::snprintf(text, sizeof(text), "%s (got %02x %02x %02x %02x %02x %02x)",
                  what, lastReply_[0], lastReply_[1], lastReply_[2],
                  lastReply_[3], lastReply_[4], lastReply_[5]);
    lastError_ = text;
}

bool SeasonicEPsu::execRead(uint8_t cmd, uint8_t dataLen, uint8_t* out)
{
    // Length is exactly dataLen. Taken from a capture of CAM itself:
    //   ad 00 02 01 60 8e  ->  aa 02 23 00   (35 degrees)
    // liquidctl sends dataLen + 1 here, which also works, but there is no
    // reason to differ from the vendor's own software.
    const uint8_t msg[6] = { kHidRead, 0x00, dataLen, 0x01, kSlaveAddress, cmd };
    uint8_t response[kReadLength];

    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        if (!transact(msg, sizeof(msg), response))
            continue;
        if (response[0] == kHidAck && response[1] == dataLen) {
            std::memcpy(out, response + 2, dataLen);
            return true;
        }
    }
    noteBadReply("unexpected PSU response");
    return false;
}

// PMBus packet error code: CRC-8, polynomial x^8 + x^2 + x + 1, taken over the
// whole SMBus write transaction including the address byte. Verified against a
// capture of CAM: address 0x60 writing 3b 26 00 produces 0x77.
uint8_t SeasonicEPsu::pec(uint8_t cmd, const uint8_t* data, uint8_t dataLen)
{
    uint8_t crc = 0;
    auto feed = [&crc](uint8_t byte) {
        crc ^= byte;
        for (int i = 0; i < 8; ++i)
            crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x07)
                               : static_cast<uint8_t>(crc << 1);
    };
    feed(static_cast<uint8_t>(kSlaveAddress << 1));   // 0x60 -> 0xC0, write bit clear
    feed(cmd);
    for (uint8_t i = 0; i < dataLen; ++i)
        feed(data[i]);
    return crc;
}

// [0xac][byte count][0x60][command][data...][pec]
// The count covers everything after the address: command, data, and the PEC.
//
// The allow-list below is the safety property this driver rests on, written
// down so it cannot drift. PMBus has registers that are genuinely dangerous to
// write: OPERATION (0x01) can switch a rail off, the fault-limit registers
// (0x40-0x4f) move overvoltage and overcurrent trip points, and the STORE_*
// commands (0x11, 0x15) commit settings to non-volatile memory, where a bad
// value survives a power cycle. None of those are reachable from here.
//
// FAN_COMMAND_1 is volatile by contrast, which is not an assumption: the
// controller demonstrably reverts to its own fan curve within seconds of the
// host going quiet, so the worst a bad value can do is last until the next
// write stops.
bool SeasonicEPsu::execWrite(uint8_t cmd, const uint8_t* data, uint8_t dataLen)
{
    if (cmd != pmbus::FAN_COMMAND_1) {
        lastError_ = "refused: only FAN_COMMAND_1 may be written";
        return false;
    }

    uint8_t msg[16] = { kHidWrite, static_cast<uint8_t>(dataLen + 2), kSlaveAddress, cmd };
    std::memcpy(msg + 4, data, dataLen);
    msg[4 + dataLen] = pec(cmd, data, dataLen);
    const size_t msgLen = 5u + dataLen;

    uint8_t response[kReadLength];
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        if (transact(msg, msgLen, response) && response[0] == kHidAck)
            return true;
    }
    noteBadReply("power supply rejected a write");
    return false;
}

bool SeasonicEPsu::beginSession()
{
    const uint8_t msg[2] = { 0xB0, 0x01 };
    uint8_t response[kReadLength];
    return transact(msg, sizeof(msg), response) && response[0] == kHidAck;
}

bool SeasonicEPsu::setFanDuty(uint8_t dutyPercent)
{
    // Clamped low as well as high. Once we are driving the fan the unit is no
    // longer running its own curve, so leaving a floor in place matters.
    if (dutyPercent < kMinFanDuty) dutyPercent = kMinFanDuty;
    if (dutyPercent > 100)         dutyPercent = 100;

    const uint8_t value[2] = { dutyPercent, 0x00 };   // little endian, as captured
    return execWrite(pmbus::FAN_COMMAND_1, value, sizeof(value));
}

bool SeasonicEPsu::execBlockRead(const uint8_t* cmd, uint8_t cmdLen, uint8_t dataLen,
                                 uint8_t* out)
{
    // A block reply is [0xaa][readLen][byte count][data...][pec], so the
    // requested read length is dataLen + 2.
    uint8_t msg[16] = { kHidRead, 0x00, static_cast<uint8_t>(dataLen + 2), cmdLen,
                        kSlaveAddress };
    std::memcpy(msg + 5, cmd, cmdLen);
    const size_t msgLen = 5u + cmdLen;

    uint8_t response[kReadLength];
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        if (!transact(msg, msgLen, response))
            continue;
        // response[2] == 0xff means "busy, data not valid" in captured traffic.
        if (response[0] == kHidAck && response[1] == dataLen + 2 && response[2] == dataLen) {
            std::memcpy(out, response + 3, dataLen);
            return true;
        }
    }
    noteBadReply("unexpected PSU block response");
    return false;
}

bool SeasonicEPsu::execPagePlusRead(uint8_t page, uint8_t cmd, uint8_t dataLen, uint8_t* out)
{
    const uint8_t block[4] = { pmbus::PAGE_PLUS_READ, 0x02, page, cmd };
    return execBlockRead(block, sizeof(block), dataLen, out);
}

// Manufacturer-specific 0xd3, which CAM polls every second as `d3 01 01` and
// `d3 01 02`. Two bytes each, steady across a 45 second capture: 0x001e and
// 0x002d. Read big-endian those are 30 and 45; read little-endian they are
// 7680 and 11520. The low byte being zero in both samples argues against a
// counter, so these are probably the per-rail OCP limits CAM exposes for the
// CPU and GPU rails rather than an hour meter. Captured, reported, not assumed.
bool SeasonicEPsu::readMfrD3(PsuStatus& status)
{
    const uint8_t first [3] = { 0xD3, 0x01, 0x01 };
    const uint8_t second[3] = { 0xD3, 0x01, 0x02 };
    uint8_t raw[2];

    bool ok = false;
    if (execBlockRead(first, sizeof(first), 2, raw)) {
        status.mfrD3a = static_cast<uint16_t>((raw[0] << 8) | raw[1]);
        ok = true;
    }
    if (execBlockRead(second, sizeof(second), 2, raw)) {
        status.mfrD3b = static_cast<uint16_t>((raw[0] << 8) | raw[1]);
        ok = true;
    }
    status.mfrD3Valid = ok;
    return ok;
}

namespace {
// A decode that lands outside these ranges means the bytes were not what we
// thought they were. Better to report nothing than to put 990 degrees on screen.
bool sane(float v, float lo, float hi) { return v >= lo && v <= hi; }
} // namespace

bool SeasonicEPsu::readRail(uint8_t page, PsuRail& rail)
{
    uint8_t mode = 0;
    if (!execPagePlusRead(page, pmbus::VOUT_MODE, 1, &mode))
        return false;
    if ((mode >> 5) != 0)      // anything but ULINEAR16 is unexpected
        return false;

    uint8_t raw[2];
    if (!execPagePlusRead(page, pmbus::READ_VOUT, 2, raw))
        return false;
    const float volts = pmbus::ulinear16(raw, mode & 0x1F);
    if (!sane(volts, 0.f, 30.f))
        return false;
    rail.volts = volts;

    if (!execPagePlusRead(page, pmbus::READ_IOUT, 2, raw))
        return false;
    const float amps = pmbus::linear11(raw);
    if (!sane(amps, -5.f, 120.f))
        return false;
    rail.amps = amps;

    if (!execPagePlusRead(page, pmbus::READ_POUT, 2, raw))
        return false;
    const float watts = pmbus::linear11(raw);
    if (!sane(watts, -5.f, 1200.f))
        return false;
    rail.watts = watts;
    return true;
}

bool SeasonicEPsu::readFirmware(PsuStatus& status)
{
    uint8_t raw[2];
    if (!execRead(pmbus::MFR_SPECIFIC_FC, 2, raw))
        return false;

    const unsigned minor = raw[0];
    const unsigned major = raw[1];

    char human[8];
    std::snprintf(human, sizeof(human), "%c%03u", static_cast<char>(major), minor);

    // CAM shows the same four characters read back as hexadecimal, so 'A017'
    // becomes 0xA017 = 40983. Parsed by hand: sscanf is deprecated under
    // /sdl, and its return value would need checking anyway.
    unsigned cam = 0;
    for (const char* p = human; *p != '\0'; ++p) {
        unsigned digit;
        if      (*p >= '0' && *p <= '9') digit = static_cast<unsigned>(*p - '0');
        else if (*p >= 'A' && *p <= 'F') digit = static_cast<unsigned>(*p - 'A' + 10);
        else if (*p >= 'a' && *p <= 'f') digit = static_cast<unsigned>(*p - 'a' + 10);
        else { cam = 0; break; }          // not hexadecimal, so no CAM-style number
        cam = (cam << 4) | digit;
    }

    std::snprintf(status.firmware.data(), status.firmware.size(), "%s/%u", human, cam);
    return true;
}

// Lifetime power-on time, in minutes.
//
// CAM reads a 239 byte block from manufacturer command 0xdc, fetched in six
// chunks: `af 00 ef 01 60 dc` opens it, then `ae 01` through `ae 06`. The block
// itself is almost entirely zeros. The counter is not in it - it sits at a
// fixed offset in each chunk *reply*, past the declared byte count, as a 32 bit
// little-endian value at bytes 43 to 46.
//
// Confirmed against CAM: a reply of `26 43 21 00` is 2,179,878 minutes, which
// is 1513 days 19 hours. CAM showed 1513D 20H forty-two minutes later.
bool SeasonicEPsu::readPowerOnMinutes(uint32_t& minutes)
{
    const uint8_t begin[6] = { 0xAF, 0x00, 0xEF, 0x01, kSlaveAddress, 0xDC };
    uint8_t response[kReadLength];
    if (!transact(begin, sizeof(begin), response) || response[0] != kHidAck)
        return false;

    bool got = false;
    for (uint8_t chunk = 1; chunk <= 6; ++chunk) {
        const uint8_t next[2] = { 0xAE, chunk };
        if (!transact(next, sizeof(next), response) || response[0] != kHidAck)
            return got;                 // walk the whole block, as CAM does
        if (chunk == 1) {
            minutes = static_cast<uint32_t>(response[43])
                    | (static_cast<uint32_t>(response[44]) << 8)
                    | (static_cast<uint32_t>(response[45]) << 16)
                    | (static_cast<uint32_t>(response[46]) << 24);
            // A power supply claiming more than a century has not been read
            // correctly. 60 million minutes is about 114 years.
            got = (minutes > 0 && minutes < 60000000u);
        }
    }
    return got;
}

bool SeasonicEPsu::poll(PsuStatus& status)
{
    if (!dev_.isOpen())
        return false;

    uint8_t raw[2];

    // Sensor 1 returns something that is not a temperature on these units;
    // liquidctl reads sensor 2 and so do we.
    if (!execRead(pmbus::READ_TEMPERATURE_2, 2, raw)) {
        status.connected = false;
        return false;
    }
    const float celsius = pmbus::linear11(raw);
    if (!sane(celsius, -40.f, 150.f)) {
        noteBadReply("PSU temperature out of range");
        status.connected = false;
        return false;
    }
    status.temperature = celsius;

    // Fan speed is a plain little-endian integer on this controller, not
    // LINEAR11. Below 2048 rpm the two readings agree, because the exponent
    // field of a LINEAR11 word is zero for any value that small - which is why
    // idle readings looked correct. Above 2047 the top bits spill into the
    // exponent and a LINEAR11 decode collapses: 2303 rpm reads back as 510,
    // so the number appears to fall as the fan speeds up.
    if (execRead(pmbus::READ_FAN_SPEED_1, 2, raw)) {
        const uint16_t rpm = static_cast<uint16_t>(raw[0] | (raw[1] << 8));
        if (rpm <= 6000)
            status.fanRpm = rpm;
    }

    if (status.firmware[0] == '\0')
        readFirmware(status);

    readMfrD3(status);

    // Moves once a minute, so there is no sense sweeping it every cycle.
    if (--uptimeCountdown_ <= 0) {
        uptimeCountdown_ = 20;
        uint32_t minutes = 0;
        if (readPowerOnMinutes(minutes)) {
            status.powerOnMinutes = minutes;
            status.powerOnValid   = true;
        }
    }

    float total = 0.f;
    for (uint8_t page = 0; page < kPsuRails; ++page) {
        if (readRail(page, status.rails[page]))
            total += status.rails[page].watts;
    }
    status.totalWatts = total;
    status.connected  = true;
    return true;
}

} // namespace lc
