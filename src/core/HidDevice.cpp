// LiquidCam - HidDevice.cpp
#include "HidDevice.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <setupapi.h>

extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}

#include <cstdio>
#include <cstring>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

namespace lc {
namespace {

std::string formatLastError(const char* what)
{
    const DWORD err = ::GetLastError();
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s failed (win32 error %lu)", what, err);
    return std::string(buf);
}

} // namespace

// ---------------------------------------------------------------------------
// Enumeration
// ---------------------------------------------------------------------------
std::vector<HidDeviceInfo> hidEnumerate(uint16_t vid, uint16_t pid)
{
    std::vector<HidDeviceInfo> found;

    GUID hidGuid;
    ::HidD_GetHidGuid(&hidGuid);

    HDEVINFO set = ::SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr,
                                          DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE)
        return found;

    SP_DEVICE_INTERFACE_DATA ifd{};
    ifd.cbSize = sizeof(ifd);

    std::vector<uint8_t> detailBuf;
    for (DWORD index = 0; ::SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, index, &ifd); ++index) {
        DWORD needed = 0;
        ::SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &needed, nullptr);
        if (needed == 0)
            continue;

        detailBuf.resize(needed);
        auto* detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(detailBuf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!::SetupDiGetDeviceInterfaceDetailW(set, &ifd, detail, needed, nullptr, nullptr))
            continue;

        // Open with no access rights: enough to query, and it never trips over
        // an exclusive lock held by another program (CAM, OpenRGB, ...).
        HANDLE h = ::CreateFileW(detail->DevicePath, 0,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            continue;

        HIDD_ATTRIBUTES attrs{};
        attrs.Size = sizeof(attrs);
        if (::HidD_GetAttributes(h, &attrs) &&
            (vid == 0 || attrs.VendorID == vid) &&
            (pid == 0 || attrs.ProductID == pid)) {

            HidDeviceInfo info;
            info.path          = detail->DevicePath;
            info.vendorId      = attrs.VendorID;
            info.productId     = attrs.ProductID;
            info.versionNumber = attrs.VersionNumber;

            PHIDP_PREPARSED_DATA pp = nullptr;
            if (::HidD_GetPreparsedData(h, &pp)) {
                HIDP_CAPS caps{};
                if (::HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS) {
                    info.usagePage       = caps.UsagePage;
                    info.usage           = caps.Usage;
                    info.inputReportLen  = caps.InputReportByteLength;
                    info.outputReportLen = caps.OutputReportByteLength;
                }
                ::HidD_FreePreparsedData(pp);
            }

            wchar_t str[128];
            str[0] = 0;
            if (::HidD_GetProductString(h, str, sizeof(str))) info.product = str;
            str[0] = 0;
            if (::HidD_GetSerialNumberString(h, str, sizeof(str))) info.serial = str;

            found.push_back(std::move(info));
        }
        ::CloseHandle(h);
    }

    ::SetupDiDestroyDeviceInfoList(set);
    return found;
}

// ---------------------------------------------------------------------------
// HidDevice
// ---------------------------------------------------------------------------
HidDevice::~HidDevice()
{
    close();
}

void HidDevice::setError(const char* what)
{
    lastError_ = formatLastError(what);
}

bool HidDevice::open(const HidDeviceInfo& info)
{
    close();

    HANDLE h = ::CreateFileW(info.path.c_str(), GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        setError("CreateFile");
        return false;
    }

    handle_ = h;
    info_   = info;

    // A deeper ring buffer means a slow poll cycle never drops a report.
    ::HidD_SetNumInputBuffers(h, 64);

    // Re-query the caps from the opened handle; enumeration may have been done
    // against a stale snapshot.
    PHIDP_PREPARSED_DATA pp = nullptr;
    if (::HidD_GetPreparsedData(h, &pp)) {
        HIDP_CAPS caps{};
        if (::HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS) {
            info_.inputReportLen  = caps.InputReportByteLength;
            info_.outputReportLen = caps.OutputReportByteLength;
        }
        ::HidD_FreePreparsedData(pp);
    }

    if (info_.inputReportLen == 0)  info_.inputReportLen  = 65;
    if (info_.outputReportLen == 0) info_.outputReportLen = 65;

    readBuf_.assign(info_.inputReportLen, 0);
    writeBuf_.assign(info_.outputReportLen, 0);

    readEvent_  = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    writeEvent_ = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    readOverlap_  = new OVERLAPPED{};
    writeOverlap_ = new OVERLAPPED{};
    static_cast<OVERLAPPED*>(readOverlap_)->hEvent  = readEvent_;
    static_cast<OVERLAPPED*>(writeOverlap_)->hEvent = writeEvent_;

    readPending_ = false;
    lastError_.clear();
    return true;
}

void HidDevice::close() noexcept
{
    if (handle_) {
        if (readPending_)
            ::CancelIoEx(handle_, static_cast<OVERLAPPED*>(readOverlap_));
        ::CloseHandle(handle_);
        handle_ = nullptr;
    }
    if (readEvent_)  { ::CloseHandle(readEvent_);  readEvent_  = nullptr; }
    if (writeEvent_) { ::CloseHandle(writeEvent_); writeEvent_ = nullptr; }
    delete static_cast<OVERLAPPED*>(readOverlap_);
    delete static_cast<OVERLAPPED*>(writeOverlap_);
    readOverlap_  = nullptr;
    writeOverlap_ = nullptr;
    readPending_  = false;
}

bool HidDevice::writeMessage(const uint8_t* msg, size_t len)
{
    if (!handle_ || len == 0)
        return false;

    const size_t reportLen = writeBuf_.size();
    std::memset(writeBuf_.data(), 0, reportLen);

    // Numbered reports: msg[0] is already the report ID, so copy it as byte 0.
    // Unnumbered reports: Windows still wants a leading 0x00 placeholder and
    // the payload shifted one to the right.
    const size_t offset = usesReportIds_ ? 0u : 1u;

    const size_t copy = (len < reportLen - offset) ? len : reportLen - offset;
    std::memcpy(writeBuf_.data() + offset, msg, copy);

    auto* ov = static_cast<OVERLAPPED*>(writeOverlap_);
    ::ResetEvent(writeEvent_);
    ov->Offset = ov->OffsetHigh = 0;

    DWORD written = 0;
    if (::WriteFile(handle_, writeBuf_.data(), static_cast<DWORD>(reportLen), &written, ov))
        return true;

    if (::GetLastError() != ERROR_IO_PENDING) {
        setError("WriteFile");
        return false;
    }

    if (::WaitForSingleObject(writeEvent_, 1000) != WAIT_OBJECT_0) {
        ::CancelIoEx(handle_, ov);
        lastError_ = "WriteFile timed out";
        return false;
    }
    if (!::GetOverlappedResult(handle_, ov, &written, FALSE)) {
        setError("GetOverlappedResult(write)");
        return false;
    }
    return true;
}

int HidDevice::readReport(uint8_t* buf, size_t len, uint32_t timeoutMs)
{
    if (!handle_)
        return -1;

    auto* ov = static_cast<OVERLAPPED*>(readOverlap_);

    if (!readPending_) {
        ::ResetEvent(readEvent_);
        ov->Offset = ov->OffsetHigh = 0;
        DWORD got = 0;
        if (!::ReadFile(handle_, readBuf_.data(), static_cast<DWORD>(readBuf_.size()), &got, ov)) {
            if (::GetLastError() != ERROR_IO_PENDING) {
                setError("ReadFile");
                return -1;
            }
            readPending_ = true;
        } else {
            const size_t copy = (got < len) ? got : len;
            std::memcpy(buf, readBuf_.data(), copy);
            return static_cast<int>(copy);
        }
    }

    // The read stays parked in the kernel while we wait: no spinning, no
    // wakeups, no CPU burnt.
    const DWORD wait = ::WaitForSingleObject(readEvent_, timeoutMs);
    if (wait == WAIT_TIMEOUT)
        return 0;                 // leave the request pending for the next call
    if (wait != WAIT_OBJECT_0) {
        setError("WaitForSingleObject");
        return -1;
    }

    DWORD got = 0;
    readPending_ = false;
    if (!::GetOverlappedResult(handle_, ov, &got, FALSE)) {
        setError("GetOverlappedResult(read)");
        return -1;
    }

    const size_t copy = (got < len) ? got : len;
    std::memcpy(buf, readBuf_.data(), copy);
    return static_cast<int>(copy);
}

void HidDevice::flushInput()
{
    if (!handle_)
        return;

    // Cancel anything parked, then let the driver drop its queue.
    if (readPending_) {
        ::CancelIoEx(handle_, static_cast<OVERLAPPED*>(readOverlap_));
        DWORD got = 0;
        ::GetOverlappedResult(handle_, static_cast<OVERLAPPED*>(readOverlap_), &got, TRUE);
        readPending_ = false;
    }
    ::HidD_FlushQueue(handle_);
}

} // namespace lc
