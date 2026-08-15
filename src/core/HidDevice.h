// LiquidCam - HidDevice.h
// Thin HID transport built straight on the Win32 HID stack (setupapi + hid.dll).
//
// Why not hidapi: one less dependency to build and ship, no per-call malloc,
// and we get to keep the OVERLAPPED read buffer alive across timeouts so a
// polling loop never has to spin. Reads block in the kernel, which is what
// keeps idle CPU at zero.
//
// windows.h is deliberately kept out of this header so it never collides with
// Qt (min/max macros, ERROR, etc.).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lc {

struct HidDeviceInfo {
    std::wstring path;
    std::wstring product;
    std::wstring serial;
    uint16_t     vendorId       = 0;
    uint16_t     productId      = 0;
    uint16_t     versionNumber  = 0;
    uint16_t     usagePage      = 0;
    uint16_t     usage          = 0;
    uint16_t     inputReportLen  = 0;  // includes the leading report-ID byte
    uint16_t     outputReportLen = 0;  // includes the leading report-ID byte
};

// Enumerate present HID interfaces. 0 acts as a wildcard for vid/pid.
std::vector<HidDeviceInfo> hidEnumerate(uint16_t vid = 0, uint16_t pid = 0);

class HidDevice {
public:
    HidDevice() noexcept = default;
    ~HidDevice();

    HidDevice(const HidDevice&)            = delete;
    HidDevice& operator=(const HidDevice&) = delete;

    bool open(const HidDeviceInfo& info);
    void close() noexcept;
    bool isOpen() const noexcept { return handle_ != nullptr; }

    const HidDeviceInfo& info() const noexcept { return info_; }
    const std::string&   lastError() const noexcept { return lastError_; }

    // Whether byte 0 of a message is a genuine report ID.
    //
    // The Smart Device uses numbered reports: its messages start with 0x01,
    // 0x02 or 0x03 and those really are report IDs. The E-series power supply
    // does not, so Windows wants a 0x00 in front and the payload shifted by
    // one. Length alone cannot tell the two apart - the Smart Device also
    // sends two-byte messages - so the driver states which it is.
    void setUsesReportIds(bool v) noexcept { usesReportIds_ = v; }

    // Sends one output report, padded to the interface's report size.
    bool writeMessage(const uint8_t* msg, size_t len);

    // Blocking read. Returns bytes copied, 0 on timeout, -1 on error.
    // buf receives the raw report, report-ID byte included, matching the
    // indexing used by liquidctl.
    int readReport(uint8_t* buf, size_t len, uint32_t timeoutMs);

    // Throw away anything the OS has already queued for us, so the next read
    // returns fresh data instead of a backlog.
    void flushInput();

private:
    void setError(const char* what);

    bool                 usesReportIds_ = true;
    void*                handle_      = nullptr;  // HANDLE
    void*                readEvent_   = nullptr;  // HANDLE, manual reset
    void*                writeEvent_  = nullptr;  // HANDLE, manual reset
    void*                readOverlap_ = nullptr;  // OVERLAPPED*
    void*                writeOverlap_ = nullptr; // OVERLAPPED*
    bool                 readPending_ = false;
    std::vector<uint8_t> readBuf_;                // stays alive across timeouts
    std::vector<uint8_t> writeBuf_;
    HidDeviceInfo        info_;
    std::string          lastError_;
};

} // namespace lc
