#include <doctest/doctest.h>
#include <cmath>
#include <cstdlib>
#include "imok_color.h"
#include "imok_color_internal.h"

using doctest::Approx;

TEST_CASE("Okhsv: black, white and grays")
{
    const ImOk::Okhsv black{ ImOk::OkLabToOkhsv({ 0.0f, 0.0f, 0.0f }) };
    CHECK(black.h == 0.0f);
    CHECK(black.s == 0.0f);
    CHECK(black.v == 0.0f);

    const ImOk::Okhsv white{ ImOk::OkLabToOkhsv(ImOk::LinearSrgbToOkLab({ 1.0f, 1.0f, 1.0f })) };
    CHECK(white.s == Approx(0.0f).epsilon(1e-4));
    CHECK(white.v == Approx(1.0f).epsilon(1e-4));

    const ImOk::Okhsv gray{ ImOk::OkLabToOkhsv(ImOk::LinearSrgbToOkLab({ 0.2f, 0.2f, 0.2f })) };
    CHECK(gray.h == 0.0f);
    CHECK(gray.s == Approx(0.0f).epsilon(1e-4));
    CHECK_FALSE(std::isnan(gray.v)); // The division Ottosson's code does here would give NaN
}

TEST_CASE("Okhsv: v = 0 is black for any hue and saturation")
{
    const ImOk::OkLab lab{ ImOk::OkhsvToOkLab({ 123.0f, 0.7f, 0.0f }) };
    CHECK(lab.L == 0.0f);
    CHECK(lab.a == 0.0f);
    CHECK(lab.b == 0.0f);
}

TEST_CASE("Okhsv: saturated cube corners have s = 1 and v = 1")
{
    const ImOk::LinearSrgb corners[]{
        { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f },
        { 1.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 1.0f }, { 1.0f, 0.0f, 1.0f } };

    for (const ImOk::LinearSrgb& corner : corners)
    {
        CAPTURE(corner.r);
        CAPTURE(corner.g);
        CAPTURE(corner.b);

        const ImOk::Okhsv hsv{ ImOk::OkLabToOkhsv(ImOk::LinearSrgbToOkLab(corner)) };
        CHECK(hsv.s == Approx(1.0f).epsilon(1e-3));
        CHECK(hsv.v == Approx(1.0f).epsilon(1e-3));
    }
}

TEST_CASE("Okhsv: round-trips from sRGB")
{
    for (int r{ 0 }; r <= 10; ++r)
    {
        for (int g{ 0 }; g <= 10; ++g)
        {
            for (int b{ 0 }; b <= 10; ++b)
            {
                const ImOk::LinearSrgb in{ r / 10.0f, g / 10.0f, b / 10.0f };
                CAPTURE(in.r);
                CAPTURE(in.g);
                CAPTURE(in.b);

                const ImOk::Okhsv hsv{ ImOk::OkLabToOkhsv(ImOk::LinearSrgbToOkLab(in)) };
                const ImOk::LinearSrgb back{ ImOk::OkLabToLinearSrgb(ImOk::OkhsvToOkLab(hsv)) };

                CHECK(back.r == Approx(in.r).epsilon(1e-3));
                CHECK(back.g == Approx(in.g).epsilon(1e-3));
                CHECK(back.b == Approx(in.b).epsilon(1e-3));
            }
        }
    }
}

// NoBrightness relies on this: constant h and s in Okhsv is a scaling of linear sRGB
TEST_CASE("Okhsv: v = 1 is the color scaled to largest channel 1")
{
    for (int r{ 0 }; r <= 10; ++r)
    {
        for (int g{ 0 }; g <= 10; ++g)
        {
            for (int b{ 0 }; b <= 10; ++b)
            {
                // Black has no chromaticity
                if (r == 0 && g == 0 && b == 0)
                {
                    continue;
                }

                // Every channel at most 0.4, so v is below 1 and raising it is tested
                const ImOk::LinearSrgb dark{ 0.04f * static_cast<float>(r), 0.04f * static_cast<float>(g),
                                             0.04f * static_cast<float>(b) };
                CAPTURE(dark.r);
                CAPTURE(dark.g);
                CAPTURE(dark.b);

                ImOk::Okhsv hsv{ ImOk::OkLabToOkhsv(ImOk::LinearSrgbToOkLab(dark)) };
                hsv.v = 1.0f;
                const ImOk::LinearSrgb raised{ ImOk::OkLabToLinearSrgb(ImOk::OkhsvToOkLab(hsv)) };
                const ImOk::LinearSrgb scaled{ ImOk::Internal::ScaleToLargestChannel(dark) };

                CHECK(raised.r == Approx(scaled.r).epsilon(1e-3));
                CHECK(raised.g == Approx(scaled.g).epsilon(1e-3));
                CHECK(raised.b == Approx(scaled.b).epsilon(1e-3));
            }
        }
    }
}

// Pure blues sit on blue's corner, where a / C and cos h, sin h differ by a few ulps (see
// ComputeMaxSaturation). #0000FF once came back as #023CFF on MSVC.
TEST_CASE("Okhsv: every pure blue has s = 1 and round-trips")
{
    for (int byte{ 1 }; byte <= 255; ++byte)
    {
        CAPTURE(byte);
        const float encoded{ static_cast<float>(byte) / 255.0f };
        const ImOk::LinearSrgb blue{ ImOk::EncodedSrgbToLinearSrgb({ 0.0f, 0.0f, encoded }) };
        const ImOk::Okhsv hsv{ ImOk::OkLabToOkhsv(ImOk::LinearSrgbToOkLab(blue)) };
        CHECK(hsv.s == Approx(1.0f).epsilon(1e-3));

        const ImOk::EncodedSrgb back{ ImOk::LinearSrgbToEncodedSrgb(ImOk::OkLabToLinearSrgb(ImOk::OkhsvToOkLab(hsv))) };
        CHECK(ImOk::Internal::ChannelToByte(back.r) <= 1);
        CHECK(ImOk::Internal::ChannelToByte(back.g) <= 1);
        CHECK(std::abs(ImOk::Internal::ChannelToByte(back.b) - byte) <= 1);
    }
}