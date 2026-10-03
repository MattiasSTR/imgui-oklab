#include <doctest/doctest.h>
#include <cstring>
#include <string>
#include "imok_internal.h"

using ImOk::Internal::ColorTooltipLines;
using ImOk::Internal::ColorTooltipText;

TEST_CASE("Tooltip: #CC4C33 in both storages")
{
    const float encoded[3]{ 204.0f / 255.0f, 76.0f / 255.0f, 51.0f / 255.0f };
    const ColorTooltipText fromEncoded{ ColorTooltipLines(encoded, nullptr, ImOkStoredAs_EncodedSrgb) };
    CHECK(std::string(fromEncoded.stored) == "Encoded sRGB (stored): 0.8000, 0.2980, 0.2000");
    CHECK(std::string(fromEncoded.hex) == "#CC4C33");
    CHECK(std::string(fromEncoded.oklch) == "oklch(58.60% 0.16761 33.00)");

    // 4 decimals are far from a rounding boundary here (0.60383, 0.07227, 0.03310), so a CRT's
    // powf an ulp off can't change the text
    const ImOk::LinearSrgb linear{ ImOk::EncodedSrgbToLinearSrgb({ encoded[0], encoded[1], encoded[2] }) };
    const float linearCol[3]{ linear.r, linear.g, linear.b };
    const ColorTooltipText fromLinear{ ColorTooltipLines(linearCol, nullptr, ImOkStoredAs_LinearSrgb) };
    CHECK(std::string(fromLinear.stored) == "Linear sRGB (stored): 0.6038, 0.0723, 0.0331");
    CHECK(std::string(fromLinear.hex) == "#CC4C33");
    CHECK(std::string(fromLinear.oklch) == "oklch(58.60% 0.16761 33.00)");
}

TEST_CASE("Tooltip: alpha as a fourth float, as-is, and in hex and oklch()")
{
    const float col[3]{ 204.0f / 255.0f, 76.0f / 255.0f, 51.0f / 255.0f };
    const float alpha{ 0.5f };
    const ColorTooltipText text{ ColorTooltipLines(col, &alpha, ImOkStoredAs_EncodedSrgb) };
    CHECK(std::string(text.stored) == "Encoded sRGB (stored): 0.8000, 0.2980, 0.2000, 0.5000");
    CHECK(std::string(text.hex) == "#CC4C3380");
    CHECK(std::string(text.oklch) == "oklch(58.60% 0.16761 33.00 / 0.500)");
}

TEST_CASE("Tooltip: col as stored, unclamped")
{
    const float hdr[3]{ 4.0f, -0.25f, 0.4f };
    CHECK(std::string(ColorTooltipLines(hdr, nullptr, ImOkStoredAs_LinearSrgb).stored)
          == "Linear sRGB (stored): 4.0000, -0.2500, 0.4000");
}

TEST_CASE("Tooltip: 4 decimals keep every linear byte level apart")
{
    std::string previous{};
    for (int k{ 0 }; k <= 255; ++k)
    {
        const float encoded{ static_cast<float>(k) / 255.0f };
        const float linear{ ImOk::EncodedSrgbToLinearSrgb({ encoded, encoded, encoded }).r };
        const float col[3]{ linear, linear, linear };
        const std::string line{ ColorTooltipLines(col, nullptr, ImOkStoredAs_LinearSrgb).stored };
        CAPTURE(k);
        REQUIRE(line != previous);
        previous = line;
    }
}

TEST_CASE("Tooltip: a light's lines, without and with a temperature")
{
    using ImOk::Internal::LightTooltipLines;
    using ImOk::Internal::LightTooltipText;

    const LightTooltipText white{ LightTooltipLines({ 1.0f, 1.0f, 1.0f }, nullptr, 800.0f, ImOkLightUnit_Lumen) };
    CHECK(std::string(white.shows) == "Color, brightness not shown");
    CHECK(std::string(white.passes) == "The color passes 100%: 800.0 lm");

    // Red passes its relative luminance, 0.2126: 170.08 lm, 1 decimal from 100 up
    const LightTooltipText red{ LightTooltipLines({ 1.0f, 0.0f, 0.0f }, nullptr, 800.0f, ImOkLightUnit_Lumen) };
    CHECK(std::string(red.passes) == "The color passes 21.3%: 170.1 lm");

    // EV100 is log2: shifted by log2(0.2126) = -2.234
    const LightTooltipText ev{ LightTooltipLines({ 1.0f, 0.0f, 0.0f }, nullptr, 10.0f, ImOkLightUnit_EV100) };
    CHECK(std::string(ev.passes) == "The color passes 21.3%: 7.77 EV");

    // Unitless has no symbol, and no space before it
    const LightTooltipText unitless{ LightTooltipLines({ 1.0f, 1.0f, 1.0f }, nullptr, 2.0f, ImOkLightUnit_Unitless) };
    CHECK(std::string(unitless.passes) == "The color passes 100%: 2.00");

    const LightTooltipText black{ LightTooltipLines({ 0.0f, 0.0f, 0.0f }, nullptr, 800.0f, ImOkLightUnit_Lumen) };
    CHECK(std::string(black.passes) == "The color passes none of this temperature");

    const float kelvin{ 2700.0f };
    CHECK(std::string(LightTooltipLines({ 1.0f, 1.0f, 1.0f }, &kelvin, 800.0f, ImOkLightUnit_Lumen).shows)
          == "Color x 2700 K, brightness not shown");
}