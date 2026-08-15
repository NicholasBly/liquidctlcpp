// LiquidCam - SeasonicEPsu.cpp
#include "SeasonicEPsu.h"
#include "Pmbus.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace lc {
    namespace {

        constexpr uint8_t kHidRead = 0xAD;   // "read" transaction, first byte of the report
        constexpr uint8_t kHidAck = 0xAA;   // first byte of a well-formed response
        constexpr uint8_t kSlaveAddress = 0x60;   // PMBus address of the PSU controller
        constexpr int     kAttempts = 3;
        constexpr long long kMinGapUs = 2500;   // the controller drops back-to-back commands

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
        size_t validBytes = static_cast<size_t>(got);
        if (response[0] == 0x00 && got > 1 && response[1] == kHidAck) {
            validBytes = static_cast<size_t>(got) - 1;
            std::memmove(response, response + 1, validBytes);
        }

        // Determine how many bytes we can safely copy (max 8)
        size_t copyBytes = sizeof(lastReply_);
        if (validBytes < copyBytes) {
            copyBytes = validBytes;
        }

        // Clear previous reply and safely copy only the available bytes
        std::memset(lastReply_, 0, sizeof(lastReply_));
        std::memcpy(lastReply_, response, copyBytes);

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
        // The length field is dataLen + 1: the controller appends a trailing byte
        // (almost certainly the PMBus PEC) that is counted but not consumed.
        const uint8_t msg[6] = { kHidRead, 0x00, static_cast<uint8_t>(dataLen + 1),
                                 0x01, kSlaveAddress, cmd };
        uint8_t response[kReadLength];

        for (int attempt = 0; attempt < kAttempts; ++attempt) {
            if (!transact(msg, sizeof(msg), response))
                continue;
            if (response[0] == kHidAck && response[1] == dataLen + 1) {
                std::memcpy(out, response + 2, dataLen);
                return true;
            }
        }
        noteBadReply("unexpected PSU response");
        return false;
    }

    bool SeasonicEPsu::execPagePlusRead(uint8_t page, uint8_t cmd, uint8_t dataLen, uint8_t* out)
    {
        // PAGE_PLUS_READ returns a block: [byte count][data...], hence dataLen + 2.
        const uint8_t msg[9] = { kHidRead, 0x00, static_cast<uint8_t>(dataLen + 2), 0x04,
                                 kSlaveAddress, pmbus::PAGE_PLUS_READ, 0x02, page, cmd };
        uint8_t response[kReadLength];

        for (int attempt = 0; attempt < kAttempts; ++attempt) {
            if (!transact(msg, sizeof(msg), response))
                continue;
            // response[2] == 0xff means "busy, data not valid" in captured traffic.
            if (response[0] == kHidAck && response[1] == dataLen + 2 && response[2] == dataLen) {
                std::memcpy(out, response + 3, dataLen);
                return true;
            }
        }
        noteBadReply("unexpected PSU rail response");
        return false;
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

        unsigned cam = 0;
        // Use the secure version and verify it successfully read 1 item
        if (sscanf_s(human, "%x", &cam) != 1) {       // 'A017' reads back as 0xA017
            cam = 0; // Fallback to 0 if parsing fails
        }

        std::snprintf(status.firmware.data(), status.firmware.size(), "%s/%u", human, cam);
        return true;
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

        if (execRead(pmbus::READ_FAN_SPEED_1, 2, raw)) {
            const float rpm = pmbus::linear11(raw);
            if (sane(rpm, 0.f, 6000.f))
                status.fanRpm = static_cast<uint16_t>(rpm);
        }

        if (status.firmware[0] == '\0')
            readFirmware(status);

        float total = 0.f;
        for (uint8_t page = 0; page < kPsuRails; ++page) {
            if (readRail(page, status.rails[page]))
                total += status.rails[page].watts;
        }
        status.totalWatts = total;
        status.connected = true;
        return true;
    }

} // namespace lc
