#include <doctest/doctest.h>
#include "imok_color.h"

namespace
{
    // True only if the braced call compiles
    template <typename T>
    constexpr auto AcceptsBracedIsInGamut(int) -> decltype(ImOk::IsInSrgbGamut({ T{}, T{}, T{} }), true) { return true; }
    template <typename T>
    constexpr bool AcceptsBracedIsInGamut(...) { return false; }

    template <typename T>
    constexpr auto AcceptsBracedClip(int)
        -> decltype(ImOk::ClipToSrgbGamut({ T{}, T{}, T{} }, ImOk::GamutClipMethod::AdaptiveMidGray), true) { return true; }
    template <typename T>
    constexpr bool AcceptsBracedClip(...) { return false; }

    template <typename T>
    constexpr auto AcceptsBracedLuminance(int) -> decltype(ImOk::RelativeLuminance({ T{}, T{}, T{} }), true) { return true; }
    template <typename T>
    constexpr bool AcceptsBracedLuminance(...) { return false; }
}

TEST_CASE("Color types default to zero")
{
    const ImOk::EncodedSrgb encoded{};
    CHECK(encoded.r == 0.0f);
    CHECK(encoded.g == 0.0f);
    CHECK(encoded.b == 0.0f);

    const ImOk::LinearSrgb linear{};
    CHECK(linear.r == 0.0f);
    CHECK(linear.g == 0.0f);
    CHECK(linear.b == 0.0f);

    const ImOk::OkLab lab{};
    CHECK(lab.L == 0.0f);
    CHECK(lab.a == 0.0f);
    CHECK(lab.b == 0.0f);

    const ImOk::OkLCh lch{};
    CHECK(lch.L == 0.0f);
    CHECK(lch.C == 0.0f);
    CHECK(lch.h == 0.0f);

    const ImOk::Okhsv hsv{};
    CHECK(hsv.h == 0.0f);
    CHECK(hsv.s == 0.0f);
    CHECK(hsv.v == 0.0f);

    const ImOk::Okhsl hsl{};
    CHECK(hsl.h == 0.0f);
    CHECK(hsl.s == 0.0f);
    CHECK(hsl.l == 0.0f);
}

TEST_CASE("Color types support brace initialization")
{
    // Distinct values per member, so a swapped member order would be caught.
    const ImOk::EncodedSrgb encoded{ 0.1f, 0.2f, 0.3f };
    CHECK(encoded.r == 0.1f);
    CHECK(encoded.g == 0.2f);
    CHECK(encoded.b == 0.3f);

    const ImOk::LinearSrgb linear{ 0.4f, 0.5f, 0.6f };
    CHECK(linear.r == 0.4f);
    CHECK(linear.g == 0.5f);
    CHECK(linear.b == 0.6f);

    const ImOk::OkLab lab{ 0.7f, -0.1f, 0.2f };
    CHECK(lab.L == 0.7f);
    CHECK(lab.a == -0.1f);
    CHECK(lab.b == 0.2f);

    const ImOk::OkLCh lch{ 0.7f, 0.15f, 30.0f };
    CHECK(lch.L == 0.7f);
    CHECK(lch.C == 0.15f);
    CHECK(lch.h == 30.0f);

    const ImOk::Okhsv hsv{ 120.0f, 0.5f, 0.8f };
    CHECK(hsv.h == 120.0f);
    CHECK(hsv.s == 0.5f);
    CHECK(hsv.v == 0.8f);

    const ImOk::Okhsl hsl{ 240.0f, 0.6f, 0.4f };
    CHECK(hsl.h == 240.0f);
    CHECK(hsl.s == 0.6f);
    CHECK(hsl.l == 0.4f);
}

// A braced { r, g, b } names no space, so functions whose name doesn't say one refuse it
static_assert(!AcceptsBracedIsInGamut<float>(0), "IsInSrgbGamut must refuse a braced { r, g, b }");
static_assert(!AcceptsBracedClip<float>(0), "ClipToSrgbGamut must refuse a braced { r, g, b }");
static_assert(!AcceptsBracedLuminance<float>(0), "RelativeLuminance must refuse a braced { r, g, b }");