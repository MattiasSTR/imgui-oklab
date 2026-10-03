#include <doctest/doctest.h>
#include <cmath>
#include "imok_color.h"

using doctest::Approx;

namespace
{
    // WCAG 2's contrast ratio, from two relative luminances
    float ContrastRatio(float lighter, float darker)
    {
        return (lighter + 0.05f) / (darker + 0.05f);
    }

    ImOk::LinearSrgb Gray8(int k)
    {
        const float encoded{ static_cast<float>(k) / 255.0f };
        return ImOk::EncodedSrgbToLinearSrgb({ encoded, encoded, encoded });
    }
}

TEST_CASE("Relative luminance: white, black and the primaries")
{
    CHECK(ImOk::RelativeLuminance(ImOk::LinearSrgb{ 1.0f, 1.0f, 1.0f }) == Approx(1.0f).epsilon(1e-6));
    CHECK(ImOk::RelativeLuminance(ImOk::LinearSrgb{ 0.0f, 0.0f, 0.0f }) == 0.0f);
    CHECK(ImOk::RelativeLuminance(ImOk::LinearSrgb{ 1.0f, 0.0f, 0.0f }) == Approx(0.2126f));
    CHECK(ImOk::RelativeLuminance(ImOk::LinearSrgb{ 0.0f, 1.0f, 0.0f }) == Approx(0.7152f));
    CHECK(ImOk::RelativeLuminance(ImOk::LinearSrgb{ 0.0f, 0.0f, 1.0f }) == Approx(0.0722f));
}

TEST_CASE("Relative luminance is not clamped")
{
    CHECK(ImOk::RelativeLuminance(ImOk::LinearSrgb{ 2.0f, 2.0f, 2.0f }) == Approx(2.0f));
}

TEST_CASE("Encoded gray 0.5: Y = 0.214, Oklab L = cbrt(Y) = 0.598")
{
    const ImOk::LinearSrgb gray{ ImOk::EncodedSrgbToLinearSrgb({ 0.5f, 0.5f, 0.5f }) };
    const float Y{ ImOk::RelativeLuminance(gray) };
    CHECK(Y == Approx(0.214041f).epsilon(1e-5));
    CHECK(ImOk::LinearSrgbToOkLab(gray).L == Approx(std::cbrt(Y)).epsilon(1e-5));
}

TEST_CASE("WCAG: #767676 on white is 4.54:1, black on white 21:1")
{
    CHECK(ContrastRatio(1.0f, ImOk::RelativeLuminance(Gray8(0x76))) == Approx(4.54f).epsilon(1e-3));
    CHECK(ContrastRatio(1.0f, 0.0f) == Approx(21.0f));
}

TEST_CASE("WCAG's older decode threshold (0.03928) agrees for every byte")
{
    // No byte lies between 0.03928 and IEC's 0.04045 (10/255 = 0.0392, 11/255 = 0.0431)
    for (int k{ 0 }; k <= 255; ++k)
    {
        const float c{ static_cast<float>(k) / 255.0f };
        const float wcag{ (c <= 0.03928f) ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f) };
        REQUIRE(Gray8(k).r == wcag);
    }
}