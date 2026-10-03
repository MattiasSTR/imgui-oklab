#include <doctest/doctest.h>
#include <cmath>
#include <cstdlib>
#include "imok_color.h"
#include "imok_color_internal.h"
#include "test_helpers.h"

using doctest::Approx;

using ImOkTest::GamutBoundaryError;

TEST_CASE("Okhsl: black, white and grays")
{
    const ImOk::Okhsl black{ ImOk::OkLabToOkhsl({ 0.0f, 0.0f, 0.0f }) };
    CHECK(black.h == 0.0f);
    CHECK(black.s == 0.0f);
    CHECK(black.l == 0.0f);

    // White has been reported to get a large saturation in the reference (division by ~0)
    const ImOk::Okhsl white{ ImOk::OkLabToOkhsl(ImOk::LinearSrgbToOkLab({ 1.0f, 1.0f, 1.0f })) };
    CHECK(white.s == Approx(0.0f).epsilon(1e-4));
    CHECK(white.l == Approx(1.0f).epsilon(1e-4));

    const ImOk::Okhsl gray{ ImOk::OkLabToOkhsl(ImOk::LinearSrgbToOkLab({ 0.2f, 0.2f, 0.2f })) };
    CHECK(gray.h == 0.0f);
    CHECK(gray.s == 0.0f);
    CHECK_FALSE(std::isnan(gray.l));
}

TEST_CASE("Okhsl: l = 0 is black and l = 1 is white for any hue and saturation")
{
    const ImOk::OkLab black{ ImOk::OkhslToOkLab({ 123.0f, 0.7f, 0.0f }) };
    CHECK(black.L == 0.0f);
    CHECK(black.a == 0.0f);
    CHECK(black.b == 0.0f);

    const ImOk::OkLab white{ ImOk::OkhslToOkLab({ 123.0f, 0.7f, 1.0f }) };
    CHECK(white.L == 1.0f);
    CHECK(white.a == 0.0f);
    CHECK(white.b == 0.0f);
}

TEST_CASE("Okhsl: l is perceptual lightness, independent of hue and saturation")
{
    // l is exactly Toe(L)
    for (int r{ 0 }; r <= 10; r += 2)
    {
        for (int g{ 0 }; g <= 10; g += 2)
        {
            for (int b{ 0 }; b <= 10; b += 2)
            {
                const ImOk::OkLab lab{ ImOk::LinearSrgbToOkLab({ r / 10.0f, g / 10.0f, b / 10.0f }) };
                CHECK(ImOk::OkLabToOkhsl(lab).l == Approx(ImOk::Toe(lab.L)).epsilon(1e-6));
            }
        }
    }
}

TEST_CASE("Okhsl: s = 1 lies on the sRGB gamut edge")
{
    // Blue's corner, where the gamut folds (see FindCusp), is measured separately.
    constexpr float BLUE_HUE{ 264.052f };
    constexpr float BLUE_WINDOW{ 1.0f };

    const float lightnesses[]{ 0.25f, 0.5f, 0.75f };
    for (const float l : lightnesses)
    {
        CAPTURE(l);

        float worstError{ 0.0f };
        float worstHue{ 0.0f };
        float worstErrorNearBlue{ 0.0f };

        for (int i{ 0 }; i < 3600; ++i)
        {
            const float deg{ i * 0.1f };
            const ImOk::LinearSrgb rgb{ ImOk::OkLabToLinearSrgb(ImOk::OkhslToOkLab({ deg, 1.0f, l })) };
            const float error{ GamutBoundaryError(rgb) };

            if (std::fabs(deg - BLUE_HUE) < BLUE_WINDOW)
            {
                worstErrorNearBlue = std::fmax(worstErrorNearBlue, error);
            }
            else if (error > worstError)
            {
                worstError = error;
                worstHue = deg;
            }
        }

        MESSAGE("l = ", l, ": worst error ", worstError, " at hue ", worstHue,
                ", near blue ", worstErrorNearBlue);
        CHECK(worstError < 5e-4f);         // Same reference accuracy as FindGamutIntersection
        CHECK(worstErrorNearBlue < 1e-3f); // Blue's corner, see FindCusp
    }
}

TEST_CASE("Okhsl: round-trips from sRGB")
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

                const ImOk::Okhsl hsl{ ImOk::OkLabToOkhsl(ImOk::LinearSrgbToOkLab(in)) };
                const ImOk::LinearSrgb back{ ImOk::OkLabToLinearSrgb(ImOk::OkhslToOkLab(hsl)) };

                CHECK(back.r == Approx(in.r).epsilon(1e-3));
                CHECK(back.g == Approx(in.g).epsilon(1e-3));
                CHECK(back.b == Approx(in.b).epsilon(1e-3));
            }
        }
    }
}

// As in Okhsv. At its own lightness, pure blue is the gamut's edge, so s = 1.
TEST_CASE("Okhsl: every pure blue has s = 1 and round-trips")
{
    for (int byte{ 1 }; byte <= 255; ++byte)
    {
        CAPTURE(byte);
        const float encoded{ static_cast<float>(byte) / 255.0f };
        const ImOk::LinearSrgb blue{ ImOk::EncodedSrgbToLinearSrgb({ 0.0f, 0.0f, encoded }) };
        const ImOk::Okhsl hsl{ ImOk::OkLabToOkhsl(ImOk::LinearSrgbToOkLab(blue)) };
        CHECK(hsl.s == Approx(1.0f).epsilon(1e-3));

        const ImOk::EncodedSrgb back{ ImOk::LinearSrgbToEncodedSrgb(ImOk::OkLabToLinearSrgb(ImOk::OkhslToOkLab(hsl))) };
        CHECK(ImOk::Internal::ChannelToByte(back.r) <= 1);
        CHECK(ImOk::Internal::ChannelToByte(back.g) <= 1);
        CHECK(std::abs(ImOk::Internal::ChannelToByte(back.b) - byte) <= 1);
    }
}