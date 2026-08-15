// LiquidCam - SmartDeviceV1.cpp
#include "SmartDeviceV1.h"

#include <cstdio>
#include <cstring>

namespace lc {
namespace {

constexpr uint8_t kCmdInit   = 0x01;   // 0x01 0x5c detect, 0x01 0x5d start reporting
constexpr uint8_t kCmdColor  = 0x02;   // 0x02 0x4b ...  lighting
constexpr uint8_t kCmdColor2 = 0x03;   // continuation packet for the LED payload
constexpr uint8_t kSubColor  = 0x4B;
constexpr uint8_t kSubFan    = 0x4D;   // 0x02 0x4d <ch> 0x00 <duty>

constexpr int kLedsPerChannel  = 40;   // the firmware always expects 40 triplets
constexpr int kColorBytes      = kLedsPerChannel * 3;
constexpr int kFirstPacketLeds = 57;   // 5 header bytes + 57 colour bytes

// The device wants GRB, not RGB.
inline void fillGrb(uint8_t* leds, Rgb c)
{
    for (int i = 0; i < kLedsPerChannel; ++i) {
        leds[i * 3 + 0] = c.g;
        leds[i * 3 + 1] = c.r;
        leds[i * 3 + 2] = c.b;
    }
}

} // namespace

bool SmartDeviceV1::open()
{
    const auto devices = hidEnumerate(kVendorId, kProductId);
    for (const auto& info : devices) {
        if (dev_.open(info))
            return true;
    }
    return false;
}

bool SmartDeviceV1::initialize(SmartDeviceStatus& status)
{
    if (!dev_.isOpen())
        return false;

    const uint8_t detect[2] = { kCmdInit, 0x5C };  // probe fans and LED accessories
    const uint8_t report[2] = { kCmdInit, 0x5D };  // start pushing status reports
    if (!write(detect, sizeof(detect)) || !write(report, sizeof(report)))
        return false;

    dev_.flushInput();

    uint8_t msg[kReadLength] = {};
    const int got = dev_.readReport(msg, sizeof(msg), 1000);
    if (got < static_cast<int>(kReadLength))
        return false;

    // NZXT reports four firmware components; CAM only shows the first and last.
    std::snprintf(status.firmware.data(), status.firmware.size(), "%u.%u",
                  static_cast<unsigned>(msg[0x0B]), static_cast<unsigned>(msg[0x0E]));

    status.ledAccessories = msg[0x11];
    status.ledType        = static_cast<uint8_t>(msg[0x10] >> 3);   // 0 = HUE+, 1 = Aer RGB
    const uint16_t ledsPerAccessory = (status.ledType == 0) ? 10 : 8;
    status.ledCount  = static_cast<uint16_t>(status.ledAccessories * ledsPerAccessory);
    status.connected = true;
    return true;
}

int SmartDeviceV1::poll(SmartDeviceStatus& status, uint32_t timeoutMs)
{
    if (!dev_.isOpen())
        return -1;

    // Drop the backlog first: the device pushes continuously and we only care
    // about the newest sample for each channel.
    dev_.flushInput();

    int reports = 0;
    uint8_t msg[kReadLength];
    for (int i = 0; i < kFanChannels; ++i) {
        const int got = dev_.readReport(msg, sizeof(msg), timeoutMs);
        if (got < 0)
            return -1;
        if (got < static_cast<int>(kReadLength))
            break;                    // timed out; keep whatever we already have
        ++reports;

        const int channel = (msg[15] >> 4);          // 0-based fan index
        if (channel < 0 || channel >= kFanChannels)
            continue;

        FanStatus& f  = status.fans[channel];
        f.controlMode = static_cast<uint8_t>(msg[15] & 0x03);
        f.rpm         = static_cast<uint16_t>((msg[3] << 8) | msg[4]);
        f.millivolts  = static_cast<uint16_t>(msg[7] * 1000 + msg[8] * 10);
        f.milliamps   = static_cast<uint16_t>(msg[9] * 1000 + msg[10] * 10);
        f.seen        = true;

        status.noiseDb = msg[1];
    }

    status.connected = true;
    return reports;
}

bool SmartDeviceV1::setFanDuty(uint8_t channel, uint8_t dutyPercent)
{
    if (!dev_.isOpen() || channel >= kFanChannels)
        return false;
    if (dutyPercent > 100)
        dutyPercent = 100;

    const uint8_t msg[5] = { kCmdColor, kSubFan, channel, 0x00, dutyPercent };
    return write(msg, sizeof(msg));
}

bool SmartDeviceV1::applyLighting(const LightingConfig& cfg)
{
    if (!dev_.isOpen())
        return false;

    const LedModeInfo& mi = ledModeInfo(cfg.mode);

    uint8_t steps = cfg.colorCount;
    if (steps < mi.minColors) steps = mi.minColors;
    if (steps > mi.maxColors) steps = mi.maxColors;
    if (mi.maxColors == 0) steps = 1;      // e.g. spectrum wave: one empty step

    const uint8_t variant =
        static_cast<uint8_t>(mi.variant | ((cfg.backward && mi.hasDirection) ? 0x10 : 0x00));

    uint8_t leds[kColorBytes];
    uint8_t packet[SmartDeviceV1::kWriteLength];

    for (uint8_t step = 0; step < steps; ++step) {
        Rgb c{};
        if (mi.maxColors > 0)
            c = scaleRgb(cfg.colors[step], cfg.brightness);
        fillGrb(leds, c);

        // byte4 packs animation speed, the step index and the marquee size.
        const uint8_t byte4 =
            static_cast<uint8_t>(static_cast<uint8_t>(cfg.speed) | (step << 5) | mi.size);

        std::memset(packet, 0, sizeof(packet));
        packet[0] = kCmdColor;
        packet[1] = kSubColor;
        packet[2] = mi.mval;
        packet[3] = variant;
        packet[4] = byte4;
        std::memcpy(packet + 5, leds, kFirstPacketLeds);
        if (!write(packet, sizeof(packet)))
            return false;

        std::memset(packet, 0, sizeof(packet));
        packet[0] = kCmdColor2;
        std::memcpy(packet + 1, leds + kFirstPacketLeds, kColorBytes - kFirstPacketLeds);
        if (!write(packet, sizeof(packet)))
            return false;
    }
    return true;
}

} // namespace lc
