// LiquidCam - Types.h
// Plain-old-data types shared between the device layer and the UI.
// Everything here is trivially copyable so a full status snapshot can be
// published to the GUI thread with a single memcpy-sized struct copy.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace lc {

// ---------------------------------------------------------------------------
// Colour
// ---------------------------------------------------------------------------
struct Rgb {
    uint8_t r = 0, g = 0, b = 0;

    constexpr bool operator==(const Rgb& o) const noexcept {
        return r == o.r && g == o.g && b == o.b;
    }
    constexpr bool operator!=(const Rgb& o) const noexcept { return !(*this == o); }
};

// Software brightness. The Smart Device V1 firmware has no brightness field,
// so CAM-style dimming is done by scaling the colour before it is sent.
constexpr Rgb scaleRgb(Rgb c, uint8_t pct) noexcept {
    return Rgb{ static_cast<uint8_t>(c.r * pct / 100),
                static_cast<uint8_t>(c.g * pct / 100),
                static_cast<uint8_t>(c.b * pct / 100) };
}

// ---------------------------------------------------------------------------
// Lighting
// ---------------------------------------------------------------------------
enum class LedMode : uint8_t {
    Off = 0,
    Fixed,
    Fading,
    SpectrumWave,
    Marquee3,
    Marquee4,
    Marquee5,
    Marquee6,
    CoveringMarquee,
    Alternating,
    MovingAlternating,
    Pulse,
    Breathing,
    Candle,
    Wings,
    Count
};

// Wire encoding for one lighting preset.
//   mval    -> byte 2 of the 0x4b command (mode)
//   variant -> byte 3 (moving flag / variant; bit 0x10 = reverse direction)
//   size    -> merged into byte 4 (marquee size)
struct LedModeInfo {
    LedMode     mode;
    const char* id;        // liquidctl name, used in the settings file
    const char* label;     // shown in the UI
    uint8_t     mval;
    uint8_t     variant;
    uint8_t     size;
    uint8_t     minColors;
    uint8_t     maxColors;
    bool        hasDirection;
};

// Table matches liquidctl's SmartDevice._COLOR_MODES (V1 protocol).
inline constexpr LedModeInfo kLedModes[] = {
    { LedMode::Off,               "off",               "Off",                0x00, 0x00, 0x00, 0, 0, false },
    { LedMode::Fixed,             "fixed",             "Fixed",              0x00, 0x00, 0x00, 1, 1, false },
    { LedMode::Fading,            "fading",            "Fading",             0x01, 0x00, 0x00, 1, 8, false },
    { LedMode::SpectrumWave,      "spectrum-wave",     "Spectrum wave",      0x02, 0x00, 0x00, 0, 0, true  },
    { LedMode::Marquee3,          "marquee-3",         "Marquee (3 LEDs)",   0x03, 0x00, 0x00, 1, 1, true  },
    { LedMode::Marquee4,          "marquee-4",         "Marquee (4 LEDs)",   0x03, 0x00, 0x08, 1, 1, true  },
    { LedMode::Marquee5,          "marquee-5",         "Marquee (5 LEDs)",   0x03, 0x00, 0x10, 1, 1, true  },
    { LedMode::Marquee6,          "marquee-6",         "Marquee (6 LEDs)",   0x03, 0x00, 0x18, 1, 1, true  },
    { LedMode::CoveringMarquee,   "covering-marquee",  "Covering marquee",   0x04, 0x00, 0x00, 1, 8, true  },
    { LedMode::Alternating,       "alternating",       "Alternating",        0x05, 0x00, 0x00, 2, 2, false },
    { LedMode::MovingAlternating, "moving-alternating","Moving alternating", 0x05, 0x08, 0x00, 2, 2, true  },
    { LedMode::Pulse,             "pulse",             "Pulse",              0x06, 0x00, 0x00, 1, 8, false },
    { LedMode::Breathing,         "breathing",         "Breathing",          0x07, 0x00, 0x00, 1, 8, false },
    { LedMode::Candle,            "candle",            "Candle",             0x09, 0x00, 0x00, 1, 1, false },
    { LedMode::Wings,             "wings",             "Wings",              0x0c, 0x00, 0x00, 1, 1, false },
};
static_assert(sizeof(kLedModes) / sizeof(kLedModes[0]) == static_cast<std::size_t>(LedMode::Count),
              "kLedModes must stay in sync with LedMode");

inline const LedModeInfo& ledModeInfo(LedMode m) noexcept {
    return kLedModes[static_cast<std::size_t>(m)];
}

enum class AnimSpeed : uint8_t { Slowest = 0, Slower, Normal, Faster, Fastest };

constexpr int kMaxLedColors = 8;

struct LightingConfig {
    LedMode                          mode       = LedMode::Fixed;
    AnimSpeed                        speed      = AnimSpeed::Normal;
    bool                             backward   = false;
    uint8_t                          brightness = 100;   // 0-100, applied in software
    uint8_t                          colorCount = 1;
    std::array<Rgb, kMaxLedColors>   colors     = { { Rgb{ 0x8B, 0x5C, 0xF6 } } };

    bool operator==(const LightingConfig& o) const noexcept {
        if (mode != o.mode || speed != o.speed || backward != o.backward ||
            brightness != o.brightness || colorCount != o.colorCount)
            return false;
        for (uint8_t i = 0; i < colorCount; ++i)
            if (colors[i] != o.colors[i]) return false;
        return true;
    }
    bool operator!=(const LightingConfig& o) const noexcept { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// Fans
// ---------------------------------------------------------------------------
enum class FanMode : uint8_t { Fixed = 0, Silent, Performance, Custom };

enum class CurveSource : uint8_t { CpuTemperature = 0, CpuLoad, None };

struct FanStatus {
    uint16_t rpm         = 0;
    uint16_t millivolts  = 0;
    uint16_t milliamps   = 0;
    uint8_t  controlMode = 0;   // 0 = not detected, 1 = DC, 2 = PWM
    uint8_t  duty        = 0;   // last duty we commanded
    bool     seen        = false;
};

constexpr int kFanChannels = 3;

struct SmartDeviceStatus {
    bool                                connected     = false;
    std::array<FanStatus, kFanChannels> fans          = {};
    uint8_t                             noiseDb       = 0;
    uint8_t                             ledAccessories = 0;
    uint8_t                             ledType       = 0;   // 0 = HUE+ strip, 1 = Aer RGB
    uint16_t                            ledCount      = 0;
    std::array<char, 16>                firmware      = {};
};

// ---------------------------------------------------------------------------
// PSU
// ---------------------------------------------------------------------------
constexpr int kPsuRails = 5;

inline constexpr const char* kPsuRailNames[kPsuRails] = {
    "+12V peripherals", "+12V EPS / ATX12V", "+12V motherboard / PCIe",
    "+5V combined", "+3.3V combined"
};

struct PsuRail {
    float volts = 0.f, amps = 0.f, watts = 0.f;
};

struct PsuStatus {
    bool                            present     = false;  // handle open
    bool                            connected   = false;  // last sweep produced data
    float                           temperature = 0.f;
    uint16_t                        fanRpm      = 0;
    std::array<PsuRail, kPsuRails>  rails       = {};
    float                           totalWatts  = 0.f;
    std::array<char, 24>            firmware    = {};
};

// ---------------------------------------------------------------------------
// Published snapshot
// ---------------------------------------------------------------------------
struct Snapshot {
    SmartDeviceStatus smart;
    PsuStatus         psu;
    float             cpuTempC  = 0.f;
    float             cpuLoadPct = 0.f;
    bool              tempValid = false;
    uint32_t          seq       = 0;
};

} // namespace lc
