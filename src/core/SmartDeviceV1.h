// LiquidCam - SmartDeviceV1.h
// Driver for the NZXT Smart Device V1 (VID 0x1e71, PID 0x1714).
//
// Three fan channels (PWM or DC, auto-detected by the firmware), one lighting
// channel driving up to 40 LEDs (4 HUE+ strips or 5 Aer RGB fans), and an
// onboard microphone reporting a noise level in dB.
//
// Protocol mirrors liquidctl's SmartDevice driver. See docs/PROTOCOL_NOTES.md.
#pragma once

#include "HidDevice.h"
#include "Types.h"

namespace lc {

class SmartDeviceV1 {
public:
    static constexpr uint16_t kVendorId = 0x1E71;
    static constexpr uint16_t kProductId = 0x1714;

    // Report sizes, report-ID byte included.
    static constexpr size_t kWriteLength = 65;
    static constexpr size_t kReadLength  = 21;

    bool open();
    void close() { dev_.close(); }
    bool isOpen() const { return dev_.isOpen(); }

    // Makes the device probe its fan headers and LED accessories, then start
    // pushing status reports. Must run at every boot and after every resume.
    bool initialize(SmartDeviceStatus& status);

    // Consumes queued status reports and folds them into `status`.
    // Returns the number of reports read, or -1 on a transport error. Zero
    // means the device stopped streaming, which is what happens after the
    // machine resumes from sleep and calls for a fresh initialize().
    int poll(SmartDeviceStatus& status, uint32_t timeoutMs);

    bool setFanDuty(uint8_t channel, uint8_t dutyPercent);
    bool applyLighting(const LightingConfig& cfg);

    const std::string& lastError() const { return dev_.lastError(); }

private:
    bool write(const uint8_t* msg, size_t len) { return dev_.writeMessage(msg, len); }

    HidDevice dev_;
};

} // namespace lc
