// LiquidCam - TempMonitor.cpp
#include "TempMonitor.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <comdef.h>
#include <wbemidl.h>

#pragma comment(lib, "wbemuuid.lib")

namespace lc {

struct TempMonitor::Impl {
    IWbemLocator*  locator  = nullptr;
    IWbemServices* services = nullptr;
    bool comInitialised     = false;
    bool zoneFound          = false;

    ULONGLONG lastIdle = 0, lastKernel = 0, lastUser = 0;
    bool      loadPrimed = false;
};

TempMonitor::TempMonitor() : impl_(new Impl) {}

TempMonitor::~TempMonitor()
{
    stop();
    delete impl_;
}

bool TempMonitor::start()
{
    if (impl_->services)
        return impl_->zoneFound;

    HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (hr == RPC_E_CHANGED_MODE)
        hr = S_OK;                       // somebody already set the apartment
    else if (SUCCEEDED(hr))
        impl_->comInitialised = true;
    if (FAILED(hr))
        return false;

    // Process-wide security only needs to be set once; a second call returns
    // RPC_E_TOO_LATE, which is harmless here.
    ::CoInitializeSecurity(nullptr, -1, nullptr, nullptr,
                           RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE,
                           nullptr, EOAC_NONE, nullptr);

    hr = ::CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                            IID_IWbemLocator, reinterpret_cast<void**>(&impl_->locator));
    if (FAILED(hr) || !impl_->locator)
        return false;

    BSTR ns = ::SysAllocString(L"ROOT\\WMI");
    hr = impl_->locator->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr,
                                       &impl_->services);
    ::SysFreeString(ns);
    if (FAILED(hr) || !impl_->services) {
        stop();
        return false;
    }

    ::CoSetProxyBlanket(impl_->services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

    float probe = 0.f;
    impl_->zoneFound = readCpuTemp(probe);
    return impl_->zoneFound;
}

void TempMonitor::stop()
{
    if (impl_->services) { impl_->services->Release(); impl_->services = nullptr; }
    if (impl_->locator)  { impl_->locator->Release();  impl_->locator  = nullptr; }
    if (impl_->comInitialised) {
        ::CoUninitialize();
        impl_->comInitialised = false;
    }
    impl_->zoneFound = false;
}

bool TempMonitor::hasTemperature() const
{
    return impl_->zoneFound;
}

bool TempMonitor::readCpuTemp(float& celsius)
{
    if (!impl_->services)
        return false;

    BSTR language = ::SysAllocString(L"WQL");
    BSTR query    = ::SysAllocString(L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature");

    IEnumWbemClassObject* enumerator = nullptr;
    HRESULT hr = impl_->services->ExecQuery(
        language, query,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr, &enumerator);

    ::SysFreeString(query);
    ::SysFreeString(language);

    if (FAILED(hr) || !enumerator)
        return false;

    bool  ok   = false;
    float best = -273.15f;

    IWbemClassObject* row = nullptr;
    ULONG returned = 0;
    while (enumerator->Next(200, 1, &row, &returned) == S_OK && returned == 1) {
        VARIANT value;
        ::VariantInit(&value);
        if (SUCCEEDED(row->Get(L"CurrentTemperature", 0, &value, nullptr, nullptr)) &&
            value.vt == VT_I4) {
            // Reported in tenths of a Kelvin.
            const float c = static_cast<float>(value.lVal) / 10.0f - 273.15f;
            if (c > best && c > -50.f && c < 150.f) {
                best = c;
                ok   = true;
            }
        }
        ::VariantClear(&value);
        row->Release();
        row = nullptr;
    }
    enumerator->Release();

    if (ok)
        celsius = best;
    return ok;
}

float TempMonitor::readCpuLoad()
{
    FILETIME idleFt, kernelFt, userFt;
    if (!::GetSystemTimes(&idleFt, &kernelFt, &userFt))
        return 0.f;

    auto toU64 = [](const FILETIME& ft) {
        return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    };

    const ULONGLONG idle   = toU64(idleFt);
    const ULONGLONG kernel = toU64(kernelFt);
    const ULONGLONG user   = toU64(userFt);

    float load = 0.f;
    if (impl_->loadPrimed) {
        const ULONGLONG dIdle  = idle   - impl_->lastIdle;
        const ULONGLONG dTotal = (kernel - impl_->lastKernel) + (user - impl_->lastUser);
        if (dTotal > 0)
            load = 100.f * static_cast<float>(dTotal - dIdle) / static_cast<float>(dTotal);
    }

    impl_->lastIdle   = idle;
    impl_->lastKernel = kernel;
    impl_->lastUser   = user;
    impl_->loadPrimed = true;

    if (load < 0.f)   load = 0.f;
    if (load > 100.f) load = 100.f;
    return load;
}

} // namespace lc
