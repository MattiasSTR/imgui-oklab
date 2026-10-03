#include <doctest/doctest.h>
#include <cmath>
#include "imok_color.h"
#include "imok_internal.h"

using doctest::Approx;

namespace
{
    // Planck's law integrated against the CIE 1931 2-degree functions at 1 nm, converted to
    // linear sRGB with no chromatic adaptation, largest channel 1: the color pbrt-v4 computes
    // spectrally. Generated offline (colour-science 0.4.7). Below 1901 K it lies outside sRGB.
    struct Reference
    {
        float kelvin{ 0.0f };
        ImOk::LinearSrgb linear{};
    };

    constexpr Reference REFERENCES[]{
        { 2700.0f, { 1.000000f, 0.415327f, 0.099154f } },
        { 6500.0f, { 1.000000f, 0.942846f, 0.992335f } },
        { 10000.0f, { 0.609233f, 0.695044f, 1.000000f } },
    };
    constexpr Reference REFERENCE_1000{ 1000.0f, { 1.000000f, 0.008610f, -0.019573f } }; // Outside sRGB

    float OkLabDistance(const ImOk::LinearSrgb& from, const ImOk::LinearSrgb& to)
    {
        const ImOk::OkLab fromLab{ ImOk::LinearSrgbToOkLab(from) };
        const ImOk::OkLab toLab{ ImOk::LinearSrgbToOkLab(to) };
        const float dL{ fromLab.L - toLab.L };
        const float da{ fromLab.a - toLab.a };
        const float db{ fromLab.b - toLab.b };
        return std::sqrt(dL * dL + da * da + db * db);
    }

    float OkLChHue(const ImOk::LinearSrgb& linear)
    {
        return ImOk::OkLabToOkLCh(ImOk::LinearSrgbToOkLab(linear)).h;
    }

    float Largest(const ImOk::LinearSrgb& linear)
    {
        return std::fmax(std::fmax(linear.r, linear.g), linear.b);
    }

    float TemperatureDistance(float fromKelvin, float toKelvin)
    {
        const ImOk::OkLab from{ ImOk::LinearSrgbToOkLab(ImOk::KelvinToLinearSrgb(fromKelvin)) };
        const ImOk::OkLab to{ ImOk::LinearSrgbToOkLab(ImOk::KelvinToLinearSrgb(toKelvin)) };
        const float dL{ from.L - to.L };
        const float da{ from.a - to.a };
        const float db{ from.b - to.b };
        return std::sqrt(dL * dL + da * da + db * db);
    }
}

TEST_CASE("Kelvin: within 0.001 of Planck's law in Oklab (measured: 0.0006 at most)")
{
    for (const Reference& reference : REFERENCES)
    {
        CAPTURE(reference.kelvin);
        CHECK(OkLabDistance(ImOk::KelvinToLinearSrgb(reference.kelvin), reference.linear) < 0.001f);
    }
}

TEST_CASE("Kelvin: the largest channel is exactly 1, every channel in [0, 1]")
{
    for (float kelvin{ ImOk::MIN_KELVIN }; kelvin <= ImOk::MAX_KELVIN; kelvin += 10.0f)
    {
        CAPTURE(kelvin);
        const ImOk::LinearSrgb linear{ ImOk::KelvinToLinearSrgb(kelvin) };
        REQUIRE(Largest(linear) == 1.0f);
        REQUIRE(linear.r >= 0.0f);
        REQUIRE(linear.g >= 0.0f);
        REQUIRE(linear.b >= 0.0f);
    }
}

TEST_CASE("Kelvin: no temperature is white; 6500 K is faintly pink")
{
    // D65 lies off the blackbody curve: the grayest temperature (~6360 K) still has chroma 0.0097
    for (float kelvin{ ImOk::MIN_KELVIN }; kelvin <= ImOk::MAX_KELVIN; kelvin += 10.0f)
    {
        CAPTURE(kelvin);
        REQUIRE(ImOk::OkLabToOkLCh(ImOk::LinearSrgbToOkLab(ImOk::KelvinToLinearSrgb(kelvin))).C > 0.009f);
    }

    const ImOk::LinearSrgb linear{ ImOk::KelvinToLinearSrgb(6500.0f) };
    CHECK(linear.r == 1.0f);
    CHECK(linear.g == Approx(0.943f).epsilon(0.002));
    CHECK(OkLChHue(linear) == Approx(330.0f).epsilon(0.005));
}

TEST_CASE("Kelvin: below 1901 K, clipped keeping hue (a per-channel clamp shifts 3.7 degrees)")
{
    const float hue{ OkLChHue(REFERENCE_1000.linear) };
    CHECK(std::fabs(OkLChHue(ImOk::KelvinToLinearSrgb(1000.0f)) - hue) < 0.2f);

    const ImOk::LinearSrgb clamped{ REFERENCE_1000.linear.r, REFERENCE_1000.linear.g, 0.0f };
    CHECK(std::fabs(OkLChHue(clamped) - hue) > 3.0f);
}

TEST_CASE("Kelvin: clamped to [MIN_KELVIN, MAX_KELVIN]")
{
    const ImOk::LinearSrgb atMin{ ImOk::KelvinToLinearSrgb(ImOk::MIN_KELVIN) };
    const ImOk::LinearSrgb belowMin{ ImOk::KelvinToLinearSrgb(500.0f) };
    CHECK(belowMin.r == atMin.r);
    CHECK(belowMin.g == atMin.g);
    CHECK(belowMin.b == atMin.b);

    const ImOk::LinearSrgb atMax{ ImOk::KelvinToLinearSrgb(ImOk::MAX_KELVIN) };
    const ImOk::LinearSrgb aboveMax{ ImOk::KelvinToLinearSrgb(40000.0f) };
    CHECK(aboveMax.r == atMax.r);
    CHECK(aboveMax.g == atMax.g);
    CHECK(aboveMax.b == atMax.b);
}

TEST_CASE("Kelvin: luminance varies with temperature; pbrt-v4 divides it away")
{
    CHECK(ImOk::RelativeLuminance(ImOk::KelvinToLinearSrgb(1900.0f)) == Approx(0.379f).epsilon(0.01));
    CHECK(ImOk::RelativeLuminance(ImOk::KelvinToLinearSrgb(2700.0f)) == Approx(0.517f).epsilon(0.01));
    CHECK(ImOk::RelativeLuminance(ImOk::KelvinToLinearSrgb(6500.0f)) == Approx(0.958f).epsilon(0.01));
    CHECK(ImOk::RelativeLuminance(ImOk::KelvinToLinearSrgb(15000.0f)) == Approx(0.585f).epsilon(0.01));

    // The recommended normalization: a 2700 K lamp keeps its intensity
    const ImOk::LinearSrgb temperature{ ImOk::KelvinToLinearSrgb(2700.0f) };
    const float Y{ ImOk::RelativeLuminance(temperature) };
    const ImOk::LinearSrgb normalized{ temperature.r / Y, temperature.g / Y, temperature.b / Y };
    CHECK(ImOk::RelativeLuminance(normalized) == Approx(1.0f).epsilon(1e-6));
}

TEST_CASE("Temperature bar: ends at MIN_KELVIN and MAX_KELVIN, round trips")
{
    CHECK(ImOk::Internal::BarPositionToKelvin(0.0f) == Approx(ImOk::MIN_KELVIN));
    CHECK(ImOk::Internal::BarPositionToKelvin(1.0f) == Approx(ImOk::MAX_KELVIN));
    CHECK(ImOk::Internal::KelvinToBarPosition(ImOk::MIN_KELVIN) == Approx(0.0f));
    CHECK(ImOk::Internal::KelvinToBarPosition(ImOk::MAX_KELVIN) == Approx(1.0f));

    for (float kelvin{ ImOk::MIN_KELVIN }; kelvin <= ImOk::MAX_KELVIN; kelvin += 7.0f)
    {
        CAPTURE(kelvin);
        REQUIRE(ImOk::Internal::BarPositionToKelvin(ImOk::Internal::KelvinToBarPosition(kelvin))
                == Approx(kelvin).epsilon(1e-5));
    }
}

TEST_CASE("Temperature bar: equal steps are equal visible changes (measured: 99 of 100 within 3%)")
{
    // 100 equal steps along the bar, in Oklab distance. One step can be shorter: at the whitest
    // temperature (~6400 K) the color's path turns, and a straight step cuts the corner.
    constexpr int STEPS{ 100 };
    float distances[STEPS]{};
    float total{ 0.0f };
    for (int i{ 0 }; i < STEPS; ++i)
    {
        distances[i] = TemperatureDistance(ImOk::Internal::BarPositionToKelvin(static_cast<float>(i) / STEPS),
                                           ImOk::Internal::BarPositionToKelvin(static_cast<float>(i + 1) / STEPS));
        total += distances[i];
    }
    const float even{ total / STEPS };
    int within{ 0 };
    for (const float distance : distances)
    {
        CHECK(distance > 0.5f * even);
        CHECK(distance < 1.1f * even);
        within += (std::fabs(distance / even - 1.0f) < 0.05f) ? 1 : 0;
    }
    CHECK(within >= STEPS - 1);
}

TEST_CASE("Temperature references: ascending, inside the range but for the off-scale sky, ASCII text")
{
    const ImOk::Internal::TemperatureReferences references{ ImOk::Internal::GetTemperatureReferences() };
    REQUIRE(references.count > 0);
    for (int i{ 0 }; i < references.count; ++i)
    {
        CAPTURE(i);
        const float value{ references.references[i].value };
        CHECK(value >= ImOk::MIN_KELVIN);
        if (i > 0)
        {
            CHECK(value > references.references[i - 1].value);
        }
        if (i < references.count - 1)
        {
            CHECK(value <= ImOk::MAX_KELVIN);
        }
        for (const char* c{ references.references[i].text }; *c != '\0'; ++c)
        {
            CHECK(static_cast<unsigned char>(*c) < 128);
        }
    }
    // The one note past the scale: a clear blue sky
    CHECK(references.references[references.count - 1].value > ImOk::MAX_KELVIN);
}