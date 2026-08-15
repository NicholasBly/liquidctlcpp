// LiquidCam - SeasonicEPsu.h
// Driver for the NZXT E-series power supplies (Seasonic OEM):
//   E500 0x7793:0x5911, E650 0x7793:0x5912, E850 0x7793:0x2500.
//
// The PSU speaks PMBus wrapped in a vendor HID transport. Every transaction is
// a request/response pair, so this device is polled on demand rather than
// streaming like the Smart Device. Monitoring only - see docs/PROTOCOL_NOTES.md
// for why fan control is not exposed.
#pragma once

#include "HidDevice.h"
#include "Types.h"

namespace lc {

class SeasonicEPsu {
public:
    static constexpr uint16_t kVendorId = 0x7793;

    static constexpr size_t kWriteLength = 64;
    static constexpr size_t kReadLength  = 64;

    bool open();
    void close() { dev_.close(); }
    bool isOpen() const { return dev_.isOpen(); }

    // One full telemetry sweep: temperature, fan, and all five rails.
    bool poll(PsuStatus& status);

    const std::string& lastError() const { return lastError_; }
    const std::wstring& modelName() const { return model_; }

private:
    // [0xad, writeLen, readLen, cmdLen, 0x60, <command bytes...>]
    bool execRead(uint8_t cmd, uint8_t dataLen, uint8_t* out);
    bool execPagePlusRead(uint8_t page, uint8_t cmd, uint8_t dataLen, uint8_t* out);
    bool transact(const uint8_t* msg, size_t len, uint8_t* response);
    void noteBadReply(const char* what);

    uint8_t lastReply_[8] = {};   // head of the last reply, for error reporting
    void pace();                       // enforces the 2.5 ms inter-command gap

    bool readRail(uint8_t page, PsuRail& rail);
    bool readFirmware(PsuStatus& status);

    HidDevice    dev_;
    std::wstring model_;
    std::string  lastError_;
    long long    lastTxUs_ = 0;
};

} // namespace lc
