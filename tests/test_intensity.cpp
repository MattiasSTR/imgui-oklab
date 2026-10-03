#include <doctest/doctest.h>
#include <cmath>
#include "imok_internal.h"

using doctest::Approx;
using ImOk::Internal::BarPositionToIntensity;
using ImOk::Internal::GetLightUnitInfo;
using ImOk::Internal::IntensityToBarPosition;
using ImOk::Internal::LightUnitInfo;
using ImOk::Internal::FilteredIntensity;
using ImOk::Internal::GetLightKindInfo;
using ImOk::Internal::IsLightUnitValid;
using ImOk::Internal::LightColorTransmittance;
using ImOk::Internal::LightSwatchColor;

namespace
{
    constexpr ImOkLightUnit LOGARITHMIC_UNITS[]{ ImOkLightUnit_Unitless, ImOkLightUnit_Lumen, ImOkLightUnit_Candela,
                                                 ImOkLightUnit_Lux, ImOkLightUnit_Nits };
}

TEST_CASE("Intensity bar: the ends are the unit's range")
{
    for (int unit{ 0 }; unit < ImOkLightUnit_COUNT; ++unit)
    {
        CAPTURE(unit);
        const LightUnitInfo& info{ GetLightUnitInfo(static_cast<ImOkLightUnit>(unit)) };
        CHECK(BarPositionToIntensity(0.0f, info) == info.minValue);
        CHECK(BarPositionToIntensity(1.0f, info) == Approx(info.maxValue));
        CHECK(IntensityToBarPosition(info.minValue, info) == 0.0f);
        CHECK(IntensityToBarPosition(info.maxValue, info) == Approx(1.0f));
        // Outside the range: clamped
        CHECK(IntensityToBarPosition(info.maxValue * 10.0f, info) == Approx(1.0f));
        CHECK(IntensityToBarPosition(info.minValue - 100.0f, info) == 0.0f);
    }
}

TEST_CASE("Intensity bar: logarithmic, each equal step the same ratio; 0 is its own zone")
{
    for (const ImOkLightUnit unit : LOGARITHMIC_UNITS)
    {
        CAPTURE(unit);
        const LightUnitInfo& info{ GetLightUnitInfo(unit) };

        // Round trips from logMin to the maximum, in ratio steps
        for (float value{ info.logMin }; value <= info.maxValue; value *= 1.37f)
        {
            CAPTURE(value);
            REQUIRE(BarPositionToIntensity(IntensityToBarPosition(value, info), info) == Approx(value).epsilon(1e-4));
        }

        // Equal steps are equal ratios
        const float a{ BarPositionToIntensity(0.4f, info) };
        const float b{ BarPositionToIntensity(0.5f, info) };
        const float c{ BarPositionToIntensity(0.6f, info) };
        CHECK(b / a == Approx(c / b).epsilon(1e-4));

        // The left zone means 0 (off); values below logMin show at its edge
        CHECK(BarPositionToIntensity(ImOk::Internal::INTENSITY_ZERO_ZONE * 0.5f, info) == 0.0f);
        CHECK(IntensityToBarPosition(info.logMin * 0.5f, info) == Approx(ImOk::Internal::INTENSITY_ZERO_ZONE));
    }
}

TEST_CASE("Intensity bar: EV100 is linear, since EV is already logarithmic")
{
    const LightUnitInfo& info{ GetLightUnitInfo(ImOkLightUnit_EV100) };
    CHECK(info.minValue < 0.0f);
    CHECK(BarPositionToIntensity(0.5f, info) == Approx(0.5f * (info.minValue + info.maxValue)));
}

TEST_CASE("Intensity references: ascending, inside the range but for off-scale notes, ASCII text")
{
    for (int unit{ 0 }; unit < ImOkLightUnit_COUNT; ++unit)
    {
        CAPTURE(unit);
        const LightUnitInfo& info{ GetLightUnitInfo(static_cast<ImOkLightUnit>(unit)) };
        REQUIRE(info.referenceCount > 0);
        for (int i{ 0 }; i < info.referenceCount; ++i)
        {
            CAPTURE(i);
            const float value{ info.references[i].value };
            CHECK(value >= info.minValue);
            if (i > 0)
            {
                CHECK(value > info.references[i - 1].value);
            }
            // Shipped sources are ASCII
            for (const char* c{ info.references[i].text }; *c != '\0'; ++c)
            {
                CHECK(static_cast<unsigned char>(*c) < 128);
            }
        }
    }
    // The sun's disk, off the scale
    const LightUnitInfo& nits{ GetLightUnitInfo(ImOkLightUnit_Nits) };
    CHECK(nits.references[nits.referenceCount - 1].value > nits.maxValue);
}

TEST_CASE("Intensity field: decimals follow the value's size")
{
    CHECK(ImOk::Internal::IntensityDecimals(0.002f) == 4);
    CHECK(ImOk::Internal::IntensityDecimals(0.1f) == 3);
    CHECK(ImOk::Internal::IntensityDecimals(3.4f) == 2);
    CHECK(ImOk::Internal::IntensityDecimals(450.0f) == 1);
    CHECK(ImOk::Internal::IntensityDecimals(110000.0f) == 0);
    CHECK(ImOk::Internal::IntensityDecimals(-2.5f) == 2);
}

TEST_CASE("Light kinds: the units each allows")
{
    // Columns: Unitless, Lumen, Candela, Lux, Nits, EV100. As listed in imok.h's ImOkLightKind.
    constexpr bool VALID[ImOkLightKind_COUNT][ImOkLightUnit_COUNT]{
        { true, true,  true,  false, false, true  }, // Point
        { true, true,  true,  false, false, true  }, // Spot
        { true, false, false, true,  false, false }, // Directional
        { true, true,  false, false, true,  true  }, // Area
        { true, false, false, false, true,  true  }, // Emissive
    };
    for (int kind{ 0 }; kind < ImOkLightKind_COUNT; ++kind)
    {
        for (int unit{ 0 }; unit < ImOkLightUnit_COUNT; ++unit)
        {
            CAPTURE(kind);
            CAPTURE(unit);
            CHECK(IsLightUnitValid(static_cast<ImOkLightKind>(kind), static_cast<ImOkLightUnit>(unit)) == VALID[kind][unit]);
        }
    }
}

TEST_CASE("Light kinds: every note is there, in ASCII")
{
    for (int kind{ 0 }; kind < ImOkLightKind_COUNT; ++kind)
    {
        CAPTURE(kind);
        const char* note{ GetLightKindInfo(static_cast<ImOkLightKind>(kind)).note };
        REQUIRE(note != nullptr);
        for (const char* c{ note }; *c != '\0'; ++c)
        {
            CHECK(static_cast<unsigned char>(*c) < 128);
        }
    }
}

TEST_CASE("Light color transmittance: white passes everything; a color passes its share")
{
    // Exactly 1: the same luminance divided by itself
    const float kelvins[]{ ImOk::MIN_KELVIN, 2700.0f, 6500.0f, ImOk::MAX_KELVIN };
    for (const float kelvin : kelvins)
    {
        CAPTURE(kelvin);
        CHECK(LightColorTransmittance({ 1.0f, 1.0f, 1.0f }, &kelvin) == 1.0f);
    }
    CHECK(LightColorTransmittance({ 1.0f, 1.0f, 1.0f }, nullptr) == 1.0f);

    // Without a temperature, the color's own luminance: blue's coefficient
    CHECK(LightColorTransmittance({ 0.0f, 0.0f, 1.0f }, nullptr) == Approx(0.0722f));

    // Over 2700 K far less, since the lamp has little blue: 1.39% from pbrt's spectral
    // reference (1, 0.4153, 0.0992)
    const float warm{ 2700.0f };
    CHECK(LightColorTransmittance({ 0.0f, 0.0f, 1.0f }, &warm) == Approx(0.0139f).epsilon(0.05));

    // A color at its largest channel 1 only removes light: never above 1
    for (float hue{ 0.0f }; hue < 360.0f; hue += 5.0f)
    {
        CAPTURE(hue);
        const ImOk::LinearSrgb vivid{ ImOk::OkLabToLinearSrgb(ImOk::OkhsvToOkLab({ hue, 1.0f, 1.0f })) };
        const ImOk::LinearSrgb color{ ImOk::Internal::Clamp01(vivid.r), ImOk::Internal::Clamp01(vivid.g),
                                      ImOk::Internal::Clamp01(vivid.b) };
        CHECK(LightColorTransmittance(color, &warm) <= 1.0f);
    }
}

TEST_CASE("Filtered intensity: scaled, or shifted in EV")
{
    CHECK(FilteredIntensity(800.0f, ImOkLightUnit_Lumen, 0.5f) == 400.0f);
    CHECK(FilteredIntensity(10.0f, ImOkLightUnit_EV100, 0.5f) == 9.0f); // Half the light: 1 EV less
    CHECK(FilteredIntensity(10.0f, ImOkLightUnit_EV100, 1.0f) == 10.0f);
}

TEST_CASE("Light swatch: color x temperature at the largest channel 1; black when nothing passes")
{
    const ImOk::LinearSrgb white{ LightSwatchColor({ 1.0f, 1.0f, 1.0f }, nullptr) };
    CHECK(white.r == 1.0f);
    CHECK(white.g == 1.0f);
    CHECK(white.b == 1.0f);

    // A white color shows the temperature itself, whose largest channel is already exactly 1
    const float warm{ 2700.0f };
    const ImOk::LinearSrgb lamp{ LightSwatchColor({ 1.0f, 1.0f, 1.0f }, &warm) };
    const ImOk::LinearSrgb temperature{ ImOk::KelvinToLinearSrgb(warm) };
    CHECK(lamp.r == temperature.r);
    CHECK(lamp.g == temperature.g);
    CHECK(lamp.b == temperature.b);

    // A darker color is shown raised: the swatch has no brightness
    const ImOk::LinearSrgb raised{ LightSwatchColor({ 0.5f, 0.25f, 0.1f }, nullptr) };
    CHECK(raised.r == 1.0f);
    CHECK(raised.g == 0.5f);
    CHECK(raised.b == Approx(0.2f));

    // Pure blue over the reddest temperature, whose blue is clipped to about 0: raised or black
    const float reddest{ ImOk::MIN_KELVIN };
    const ImOk::LinearSrgb blue{ LightSwatchColor({ 0.0f, 0.0f, 1.0f }, &reddest) };
    const float largest{ std::fmax(std::fmax(blue.r, blue.g), blue.b) };
    CHECK((largest == 1.0f || largest == 0.0f));
}