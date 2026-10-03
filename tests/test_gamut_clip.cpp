#include <doctest/doctest.h>
#include <cmath>
#include "imok_color.h"
#include "test_helpers.h"

using doctest::Approx;

namespace
{
    constexpr ImOk::GamutClipMethod ALL_METHODS[]{
        ImOk::GamutClipMethod::PreserveLightness,
        ImOk::GamutClipMethod::ProjectToMidGray,
        ImOk::GamutClipMethod::ProjectToCuspLightness,
        ImOk::GamutClipMethod::AdaptiveMidGray,
        ImOk::GamutClipMethod::AdaptiveCuspLightness,
    };

    bool IsInGamutWithTolerance(const ImOk::LinearSrgb& c, float tolerance)
    {
        return c.r >= -tolerance && c.r <= 1.0f + tolerance
            && c.g >= -tolerance && c.g <= 1.0f + tolerance
            && c.b >= -tolerance && c.b <= 1.0f + tolerance;
    }

    using ImOkTest::HueDistance;
}

TEST_CASE("IsInSrgbGamut includes the boundary")
{
    CHECK(ImOk::IsInSrgbGamut(ImOk::LinearSrgb{ 0.2f, 0.5f, 0.7f }));
    CHECK(ImOk::IsInSrgbGamut(ImOk::LinearSrgb{ 0.0f, 0.0f, 0.0f }));
    CHECK(ImOk::IsInSrgbGamut(ImOk::LinearSrgb{ 1.0f, 1.0f, 1.0f }));
    CHECK(ImOk::IsInSrgbGamut(ImOk::LinearSrgb{ 0.0f, 0.0f, 1.0f }));

    CHECK_FALSE(ImOk::IsInSrgbGamut(ImOk::LinearSrgb{ 1.001f, 0.5f, 0.5f }));
    CHECK_FALSE(ImOk::IsInSrgbGamut(ImOk::LinearSrgb{ 0.5f, -0.001f, 0.5f }));
}

TEST_CASE("Gamut clip leaves in-gamut colors untouched, boundary included")
{
    // Boundary colors: Ottosson's clip would move them
    const ImOk::LinearSrgb colors[]{
        { 0.2f, 0.5f, 0.7f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f },
        { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };

    for (const ImOk::GamutClipMethod method : ALL_METHODS)
    {
        CAPTURE(static_cast<int>(method));
        for (const ImOk::LinearSrgb& color : colors)
        {
            const ImOk::LinearSrgb clipped{ ImOk::ClipToSrgbGamut(color, method) };
            CHECK(clipped.r == color.r);
            CHECK(clipped.g == color.g);
            CHECK(clipped.b == color.b);
        }
    }
}

TEST_CASE("Gamut clip of out-of-range grays clamps them")
{
    for (const ImOk::GamutClipMethod method : ALL_METHODS)
    {
        CAPTURE(static_cast<int>(method));

        const ImOk::LinearSrgb hdrWhite{ ImOk::ClipToSrgbGamut(ImOk::LinearSrgb{ 2.0f, 2.0f, 2.0f }, method) };
        CHECK(hdrWhite.r == 1.0f);
        CHECK(hdrWhite.g == 1.0f);
        CHECK(hdrWhite.b == 1.0f);

        const ImOk::LinearSrgb belowBlack{ ImOk::ClipToSrgbGamut(ImOk::LinearSrgb{ -0.1f, -0.1f, -0.1f }, method) };
        CHECK(belowBlack.r == 0.0f);
        CHECK(belowBlack.g == 0.0f);
        CHECK(belowBlack.b == 0.0f);
    }
}

TEST_CASE("Gamut clip brings out-of-gamut colors inside and keeps their hue")
{
    for (const ImOk::GamutClipMethod method : ALL_METHODS)
    {
        CAPTURE(static_cast<int>(method));

        for (int hue{ 0 }; hue < 360; hue += 10)
        {
            const float lightnesses[]{ 0.2f, 0.5f, 0.8f };
            for (const float L : lightnesses)
            {
                CAPTURE(hue);
                CAPTURE(L);

                // Chroma 0.4 is outside sRGB for every hue
                const ImOk::OkLCh lch{ L, 0.4f, static_cast<float>(hue) };
                const ImOk::LinearSrgb outside{ ImOk::OkLabToLinearSrgb(ImOk::OkLChToOkLab(lch)) };
                REQUIRE_FALSE(ImOk::IsInSrgbGamut(outside));

                const ImOk::LinearSrgb clipped{ ImOk::ClipToSrgbGamut(outside, method) };
                CHECK(IsInGamutWithTolerance(clipped, 1e-3f));

                const ImOk::OkLCh clippedLch{ ImOk::OkLabToOkLCh(ImOk::LinearSrgbToOkLab(clipped)) };
                CHECK(HueDistance(clippedLch.h, lch.h) < 0.1f);

                if (method == ImOk::GamutClipMethod::PreserveLightness)
                {
                    CHECK(clippedLch.L == Approx(L).epsilon(1e-3));
                }
            }
        }
    }
}