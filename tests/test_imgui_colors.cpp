#include <doctest/doctest.h>
#include <cmath>
#include "imok.h"
#include "test_helpers.h"

namespace
{
    using ImOkTest::HueDistance;
    float HueOf(const ImOk::LinearSrgb& c)
    {
        return ImOk::OkLabToOkLCh(ImOk::LinearSrgbToOkLab(c)).h;
    }

    // True only if the braced call compiles; the deleted overload makes it false
    template <typename T>
    constexpr auto AcceptsBracedAlphaBar(int)
        -> decltype(ImOk::AddRectAlphaBar(nullptr, ImVec2(), ImVec2(), { T{}, T{}, T{} }), true) { return true; }
    template <typename T>
    constexpr bool AcceptsBracedAlphaBar(...) { return false; }
}

static_assert(!AcceptsBracedAlphaBar<float>(0), "AddRectAlphaBar must refuse a braced { r, g, b }");

TEST_CASE("LinearSrgbToImVec4 draws as the same bytes as LinearSrgbToImU32")
{
    // -0.25 to 1.5: in gamut, HDR and negative channels
    constexpr int STEPS{ 14 };
    int mismatches{ 0 };
    for (int r{ 0 }; r <= STEPS; ++r)
    {
        for (int g{ 0 }; g <= STEPS; ++g)
        {
            for (int b{ 0 }; b <= STEPS; ++b)
            {
                const ImOk::LinearSrgb c{ -0.25f + 0.125f * static_cast<float>(r),
                                          -0.25f + 0.125f * static_cast<float>(g),
                                          -0.25f + 0.125f * static_cast<float>(b) };
                if (ImGui::ColorConvertFloat4ToU32(ImOk::LinearSrgbToImVec4(c)) != ImOk::LinearSrgbToImU32(c))
                {
                    ++mismatches;
                }
            }
        }
    }
    CHECK(mismatches == 0);
}

TEST_CASE("LinearSrgbToImVec4 keeps full precision")
{
    // Through 8 bits, encoded 0.3 would come back as 77 / 255 = 0.30196
    const ImVec4 v{ ImOk::LinearSrgbToImVec4(ImOk::EncodedSrgbToLinearSrgb({ 0.3f, 0.3f, 0.3f }), 0.3f) };
    CHECK(v.x == doctest::Approx(0.3f).epsilon(1e-6));
    CHECK(v.w == 0.3f);
}

TEST_CASE("LinearSrgbToImVec4 clips HDR keeping hue, or clamps with the flag")
{
    const ImOk::LinearSrgb hdr{ 2.0f, 1.0f, 0.5f };

    const ImVec4 clipped{ ImOk::LinearSrgbToImVec4(hdr) };
    CHECK(clipped.x <= 1.0f);
    CHECK(clipped.y <= 1.0f);
    CHECK(clipped.z <= 1.0f);
    CHECK(HueDistance(HueOf(hdr), HueOf(ImOk::ImVec4ToLinearSrgb(clipped))) < 0.5f);

    const ImVec4 clamped{ ImOk::LinearSrgbToImVec4(hdr, 1.0f, ImOkDrawFlags_ClampOutOfGamut) };
    CHECK(clamped.x == 1.0f);
    CHECK(clamped.y == 1.0f);
}

TEST_CASE("LinearSrgbToImU32 clamps rounding-sized overshoot, and clips beyond it")
{
    // An Okhsv color next to blue's hue, 4e-4 below 0: clipped keeping hue it would be
    // (0, 0, 249); clamped it is what the widgets store, (0, 42, 230)
    const ImOk::LinearSrgb overshoot{ -0.0004f, 0.0233f, 0.7885f };
    CHECK(ImOk::LinearSrgbToImU32(overshoot) == ImOk::LinearSrgbToImU32(overshoot, 1.0f, ImOkDrawFlags_ClampOutOfGamut));

    // Past the tolerance: a real out-of-gamut color, still clipped
    const ImOk::LinearSrgb outside{ -0.002f, 0.0233f, 0.7885f };
    CHECK(ImOk::LinearSrgbToImU32(outside) != ImOk::LinearSrgbToImU32(outside, 1.0f, ImOkDrawFlags_ClampOutOfGamut));
}

TEST_CASE("ImU32ToLinearSrgb decodes every byte as k / 255, in ImGui's channel order")
{
    for (int k{ 0 }; k <= 255; ++k)
    {
        const float encoded{ static_cast<float>(k) / 255.0f };
        const ImOk::LinearSrgb expected{ ImOk::EncodedSrgbToLinearSrgb({ encoded, encoded, encoded }) };
        const ImOk::LinearSrgb fromU32{ ImOk::ImU32ToLinearSrgb(IM_COL32(k, k, k, 255)) };
        REQUIRE(fromU32.r == expected.r);
        REQUIRE(fromU32.g == expected.g);
        REQUIRE(fromU32.b == expected.b);
    }

    const ImOk::LinearSrgb red{ ImOk::ImU32ToLinearSrgb(IM_COL32(255, 0, 0, 0)) };
    CHECK(red.r == doctest::Approx(1.0f).epsilon(1e-6));
    CHECK(red.g == 0.0f);
    CHECK(red.b == 0.0f);
}

TEST_CASE("ImVec4 round trip")
{
    for (int i{ 0 }; i <= 1000; ++i)
    {
        const float v{ static_cast<float>(i) / 1000.0f };
        const ImVec4 back{ ImOk::LinearSrgbToImVec4(ImOk::ImVec4ToLinearSrgb(ImVec4(v, v, v, 1.0f))) };
        CHECK(back.x == doctest::Approx(v).epsilon(1e-5));
    }
}