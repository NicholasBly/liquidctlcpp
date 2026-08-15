// LiquidCam - Pmbus.h
// The PMBus subset the Seasonic-built NZXT E-series power supplies answer to.
#pragma once

#include <cmath>
#include <cstdint>

namespace lc {
namespace pmbus {

enum Cmd : uint8_t {
    PAGE               = 0x00,
    PAGE_PLUS_READ     = 0x06,
    VOUT_MODE          = 0x20,
    READ_VIN           = 0x88,
    READ_IIN           = 0x89,
    READ_VOUT          = 0x8B,
    READ_IOUT          = 0x8C,
    READ_TEMPERATURE_1 = 0x8D,
    READ_TEMPERATURE_2 = 0x8E,
    READ_TEMPERATURE_3 = 0x8F,
    READ_FAN_SPEED_1   = 0x90,
    READ_POUT          = 0x96,
    READ_PIN           = 0x97,
    MFR_SPECIFIC_FC    = 0xFC,
};

// LINEAR11: 5-bit signed exponent in the top bits, 11-bit signed mantissa.
inline float linear11(const uint8_t* le2) noexcept
{
    const uint16_t raw = static_cast<uint16_t>(le2[0] | (le2[1] << 8));
    int exponent = static_cast<int>(raw >> 11);
    int mantissa = static_cast<int>(raw & 0x07FF);
    if (mantissa > 1023) mantissa -= 2048;
    if (exponent > 15)   exponent -= 32;
    return std::ldexp(static_cast<float>(mantissa), exponent);
}

// ULINEAR16: unsigned 16-bit mantissa, exponent carried by VOUT_MODE.
inline float ulinear16(const uint8_t* le2, int exponent) noexcept
{
    const uint16_t raw = static_cast<uint16_t>(le2[0] | (le2[1] << 8));
    if (exponent > 15) exponent -= 32;
    return std::ldexp(static_cast<float>(raw), exponent);
}

} // namespace pmbus
} // namespace lc
