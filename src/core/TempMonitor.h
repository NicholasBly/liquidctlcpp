// LiquidCam - TempMonitor.h
// Curve inputs. Two sources, both cheap:
//   * CPU package temperature via the ACPI thermal zone exposed in WMI. Not
//     every board publishes one, so this can legitimately be unavailable.
//   * CPU load from GetSystemTimes, which costs two syscalls and always works.
//
// start() connects to WMI and must be called on the thread that will do the
// reading, because the COM apartment is per-thread.
#pragma once

namespace lc {

class TempMonitor {
public:
    TempMonitor();
    ~TempMonitor();

    TempMonitor(const TempMonitor&)            = delete;
    TempMonitor& operator=(const TempMonitor&) = delete;

    bool start();                     // true if a thermal zone was found
    void stop();
    bool hasTemperature() const;

    bool  readCpuTemp(float& celsius);   // false when no zone is published
    float readCpuLoad();                 // 0-100, averaged over the interval

private:
    struct Impl;
    Impl* impl_;
};

} // namespace lc
