// LiquidCam - Settings.h
// User preferences, persisted to an INI next to the user's roaming profile.
#pragma once

#include <QString>
#include <array>

#include "core/FanCurve.h"
#include "core/Types.h"

namespace lc {

struct ChannelSettings {
    FanMode  mode      = FanMode::Fixed;
    int      fixedDuty = 40;
    FanCurve curve     = FanCurve::defaultCustom();
};

struct AppSettings {
    // Lighting
    LightingConfig lighting;
    bool           applyLightingAtStartup = true;

    // Cooling
    std::array<ChannelSettings, kFanChannels> channels;
    bool        applyFansAtStartup = true;
    CurveSource curveSource        = CurveSource::CpuTemperature;
    int         fallbackTemp       = 45;   // used when no sensor is available
    int         minDuty            = 20;   // floor, so fans never fully stop

    // Power supply fan. Off by default: taking control means taking
    // responsibility for cooling it, so it is opt in.
    PsuFanMode psuFanMode  = PsuFanMode::DeviceCurve;
    int        psuFixedPct = 40;
    FanCurve   psuCurve    = FanCurve::silent();

    // Polling
    int pollIntervalMs = 1000;
    int psuPollEvery   = 3;    // PSU sweep every N poll ticks
    int idleMultiplier = 3;    // slow down while the window is hidden

    // Windows integration
    bool startWithWindows = false;
    bool startMinimized   = false;
    bool minimizeToTray   = true;
};

namespace settings {

QString filePath();
void    load(AppSettings& out);
void    save(const AppSettings& in);

// HKCU\Software\Microsoft\Windows\CurrentVersion\Run
bool runAtStartup();
void setRunAtStartup(bool enabled);

const char* ledModeId(LedMode mode);
LedMode     ledModeFromId(const QString& id);

} // namespace settings
} // namespace lc
