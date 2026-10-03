#include <doctest/doctest.h>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <limits>
#include "imok_internal.h"
#include "test_helpers.h"

// What ImGui itself sends and accepts is pinned by the widget tests

namespace
{
    using ImOk::Internal::ApplyColorPayload;
    using ImOk::Internal::DropResult;
    using ImOk::Internal::StoredToEncodedSrgb;

    // Distance between two floats in units in the last place (same sign assumed)
    int64_t Ulps(float a, float b)
    {
        int32_t ia{ 0 };
        int32_t ib{ 0 };
        std::memcpy(&ia, &a, sizeof(float));
        std::memcpy(&ib, &b, sizeof(float));
        return std::llabs(static_cast<int64_t>(ia) - static_cast<int64_t>(ib));
    }

    using ImOkTest::HueDistance;

    float HueOf(const ImOk::LinearSrgb& c)
    {
        return ImOk::OkLabToOkLCh(ImOk::LinearSrgbToOkLab(c)).h;
    }

    void EncodedPayload(const ImOk::LinearSrgb& linear, float payload[3])
    {
        const ImOk::EncodedSrgb encoded{ ImOk::LinearSrgbToEncodedSrgb(linear) };
        payload[0] = encoded.r;
        payload[1] = encoded.g;
        payload[2] = encoded.b;
    }
}

TEST_CASE("EncodedSrgb storage: send then receive is bit-exact")
{
    // Every 8-bit value and the floats between them: in-range payloads are copied, never converted
    constexpr int SAMPLES{ 1 << 16 };
    for (int i{ 0 }; i <= SAMPLES; ++i)
    {
        const float v{ static_cast<float>(i) / SAMPLES };
        const float source[3]{ v, 1.0f - v, v * 0.5f };
        const ImOk::EncodedSrgb payload{ StoredToEncodedSrgb(source, ImOkStoredAs_EncodedSrgb) };
        const float data[3]{ payload.r, payload.g, payload.b };
        float target[3]{ -1.0f, -1.0f, -1.0f };
        ApplyColorPayload(data, 3, ImOkStoredAs_EncodedSrgb, target, nullptr);
        REQUIRE(target[0] == source[0]);
        REQUIRE(target[1] == source[1]);
        REQUIRE(target[2] == source[2]);
    }
}

TEST_CASE("LinearSrgb storage: send then receive is within 8 ulp")
{
    // Measured over 2^24 values: at most 4 ulp, 41.5% bit-exact; 8 with FMA contraction
    constexpr int SAMPLES{ 1 << 20 };
    int64_t worstUlps{ 0 };
    for (int i{ 0 }; i <= SAMPLES; ++i)
    {
        const float v{ static_cast<float>(i) / SAMPLES };
        const float source[3]{ v, v, v };
        const ImOk::EncodedSrgb payload{ StoredToEncodedSrgb(source, ImOkStoredAs_LinearSrgb) };
        const float data[3]{ payload.r, payload.g, payload.b };
        float target[3]{};
        ApplyColorPayload(data, 3, ImOkStoredAs_LinearSrgb, target, nullptr);
        worstUlps = std::max(worstUlps, Ulps(source[0], target[0]));
    }
    MESSAGE("Worst linear round trip: ", worstUlps, " ulp");
    CHECK(worstUlps <= 8);
}

TEST_CASE("The payload is the exact stored value, not the 8-bit swatch color")
{
    // The 8-bit swatch color would be 31/255 = 0.121568627
    const float source[3]{ 0.2f, 0.4f, 0.123456789f };
    const ImOk::EncodedSrgb payload{ StoredToEncodedSrgb(source, ImOkStoredAs_EncodedSrgb) };
    CHECK(payload.b == source[2]);

    // LinearSrgb storage sends the encoded value, at full precision
    const float linear[3]{ 0.5f, 0.5f, 0.5f };
    const ImOk::EncodedSrgb encoded{ StoredToEncodedSrgb(linear, ImOkStoredAs_LinearSrgb) };
    CHECK(encoded.r == doctest::Approx(0.735356983f).epsilon(1e-6));
}

TEST_CASE("Sending never clamps: out-of-range stored values go out as they are")
{
    const float hdr[3]{ 2.0f, -0.1f, 0.5f };
    const ImOk::EncodedSrgb payload{ StoredToEncodedSrgb(hdr, ImOkStoredAs_LinearSrgb) };
    CHECK(payload.r > 1.0f);
    CHECK(payload.g < 0.0f); // Sign-preserving encode
}

TEST_CASE("An in-range payload on the gamut edge is not moved by clipping")
{
    // (1, 0.5, 0) encoded: decoding 1.0 may land a hair above 1. Clamping absorbs that;
    // clipping would move the color by up to its accuracy (~5e-4).
    const float data[3]{ 1.0f, 0.5f, 0.0f };
    float target[3]{};
    ApplyColorPayload(data, 3, ImOkStoredAs_LinearSrgb, target, nullptr);
    const ImOk::LinearSrgb decoded{ ImOk::EncodedSrgbToLinearSrgb({ 0.0f, 0.5f, 0.0f }) };
    CHECK(target[0] == 1.0f);
    CHECK(target[1] == decoded.g);
    CHECK(target[2] == 0.0f);
}

TEST_CASE("HDR payloads are brought inside sRGB keeping their hue")
{
    const ImOk::LinearSrgb hdr{ 2.0f, 1.0f, 0.5f }; // An orange, brighter than the display
    float data[3]{};
    EncodedPayload(hdr, data);

    SUBCASE("LinearSrgb storage")
    {
        float target[3]{};
        const DropResult result{ ApplyColorPayload(data, 3, ImOkStoredAs_LinearSrgb, target, nullptr) };
        CHECK(result.rgbChanged);
        for (float c : target)
        {
            CHECK(c >= 0.0f);
            CHECK(c <= 1.0f);
        }
        const float hueError{ HueDistance(HueOf(hdr), HueOf({ target[0], target[1], target[2] })) };
        MESSAGE("Hue error after clipping: ", hueError, " degrees");
        CHECK(hueError < 0.5f);

        // Why not clamp: per channel it becomes (1, 1, 0.5), a yellow
        const float clampError{ HueDistance(HueOf(hdr), HueOf({ 1.0f, 1.0f, 0.5f })) };
        MESSAGE("Hue error if clamped instead: ", clampError, " degrees");
        CHECK(clampError > 20.0f);
    }

    SUBCASE("EncodedSrgb storage")
    {
        float target[3]{};
        ApplyColorPayload(data, 3, ImOkStoredAs_EncodedSrgb, target, nullptr);
        for (float c : target)
        {
            CHECK(c >= 0.0f);
            CHECK(c <= 1.0f);
        }
        const ImOk::LinearSrgb stored{ ImOk::EncodedSrgbToLinearSrgb({ target[0], target[1], target[2] }) };
        CHECK(HueDistance(HueOf(hdr), HueOf(stored)) < 0.5f);
    }
}

TEST_CASE("Out-of-gamut payloads with a negative channel keep their hue")
{
    const ImOk::LinearSrgb wide{ -0.1f, 0.6f, 0.2f }; // A green outside sRGB
    float data[3]{};
    EncodedPayload(wide, data);
    float target[3]{};
    ApplyColorPayload(data, 3, ImOkStoredAs_LinearSrgb, target, nullptr);
    for (float c : target)
    {
        CHECK(c >= 0.0f);
        CHECK(c <= 1.0f);
    }
    CHECK(HueDistance(HueOf(wide), HueOf({ target[0], target[1], target[2] })) < 0.5f);
}

TEST_CASE("A payload with any non-finite value is ignored whole")
{
    const float nan{ std::numeric_limits<float>::quiet_NaN() };
    const float infinity{ std::numeric_limits<float>::infinity() };
    const float payloads[][4]{
        { nan, 0.5f, 0.5f, 1.0f },
        { 0.5f, infinity, 0.5f, 1.0f },
        { 0.5f, 0.5f, -infinity, 1.0f },
        { 0.5f, 0.5f, 0.5f, nan }, // Alpha too: no half-applied drop
    };
    for (const auto& payload : payloads)
    {
        float col[3]{ 0.1f, 0.2f, 0.3f };
        float alpha{ 0.4f };
        const DropResult result{ ApplyColorPayload(payload, 4, ImOkStoredAs_EncodedSrgb, col, &alpha) };
        CHECK_FALSE(result.rgbChanged);
        CHECK_FALSE(result.alphaChanged);
        CHECK(col[0] == 0.1f);
        CHECK(col[1] == 0.2f);
        CHECK(col[2] == 0.3f);
        CHECK(alpha == 0.4f);
    }
}

TEST_CASE("Alpha: 3F keeps it, 4F sets it, 3-variants ignore it")
{
    const float rgb3[3]{ 0.25f, 0.5f, 0.75f };
    const float rgba4[4]{ 0.25f, 0.5f, 0.75f, 0.3f };

    SUBCASE("3 floats onto a 4-variant: alpha stays")
    {
        float col[3]{};
        float alpha{ 0.9f };
        const DropResult result{ ApplyColorPayload(rgb3, 3, ImOkStoredAs_EncodedSrgb, col, &alpha) };
        CHECK(result.rgbChanged);
        CHECK_FALSE(result.alphaChanged);
        CHECK(alpha == 0.9f);
    }

    SUBCASE("4 floats onto a 4-variant: alpha set, as-is (never encoded)")
    {
        float col[3]{};
        float alpha{ 0.9f };
        const DropResult result{ ApplyColorPayload(rgba4, 4, ImOkStoredAs_LinearSrgb, col, &alpha) };
        CHECK(result.alphaChanged);
        CHECK(alpha == 0.3f);
    }

    SUBCASE("4 floats onto a 3-variant: payload alpha ignored")
    {
        float col[3]{};
        const DropResult result{ ApplyColorPayload(rgba4, 4, ImOkStoredAs_EncodedSrgb, col, nullptr) };
        CHECK(result.rgbChanged);
        CHECK_FALSE(result.alphaChanged);
    }

    SUBCASE("Alpha is clamped to [0, 1]")
    {
        const float over[4]{ 0.25f, 0.5f, 0.75f, 1.5f };
        const float under[4]{ 0.25f, 0.5f, 0.75f, -0.5f };
        float col[3]{};
        float alpha{ 0.5f };
        ApplyColorPayload(over, 4, ImOkStoredAs_EncodedSrgb, col, &alpha);
        CHECK(alpha == 1.0f);
        ApplyColorPayload(under, 4, ImOkStoredAs_EncodedSrgb, col, &alpha);
        CHECK(alpha == 0.0f);
    }
}

TEST_CASE("A drop that changes only alpha leaves col bit-identical")
{
    // The surface value is derived from col; alpha derives nothing. An alpha-only drop must
    // not count as a color edit, or hue would reset at grays.
    float col[3]{ 0.5f, 0.5f, 0.5f };
    float alpha{ 1.0f };
    const float payload[4]{ 0.5f, 0.5f, 0.5f, 0.25f };
    const DropResult result{ ApplyColorPayload(payload, 4, ImOkStoredAs_EncodedSrgb, col, &alpha) };
    CHECK_FALSE(result.rgbChanged);
    CHECK(result.alphaChanged);
    CHECK(col[0] == 0.5f);
    CHECK(col[1] == 0.5f);
    CHECK(col[2] == 0.5f);
}

TEST_CASE("A new color keeps what it doesn't define")
{
    const ImOk::OkLab gray{ ImOk::LinearSrgbToOkLab(ImOk::EncodedSrgbToLinearSrgb({ 0.5f, 0.5f, 0.5f })) };
    const ImOk::OkLab black{ ImOk::LinearSrgbToOkLab({ 0.0f, 0.0f, 0.0f }) };
    const ImOk::OkLab white{ ImOk::LinearSrgbToOkLab({ 1.0f, 1.0f, 1.0f }) };
    const ImOk::OkLab orange{ ImOk::LinearSrgbToOkLab(ImOk::EncodedSrgbToLinearSrgb({ 1.0f, 0.5f, 0.1f })) };

    SUBCASE("Okhsv")
    {
        const ImOk::Okhsv current{ 250.0f, 0.8f, 0.7f };

        // Gray: hue stays, saturation goes to exactly 0 (the conversion alone gives ~7e-7)
        const ImOk::Okhsv onGray{ ImOk::Internal::OkLabToOkhsvKeeping(gray, current) };
        CHECK(onGray.h == current.h);
        CHECK(onGray.s == 0.0f);
        CHECK(onGray.v == doctest::Approx(ImOk::OkLabToOkhsv(gray).v));

        // Black: hue and saturation stay
        const ImOk::Okhsv onBlack{ ImOk::Internal::OkLabToOkhsvKeeping(black, current) };
        CHECK(onBlack.h == current.h);
        CHECK(onBlack.s == current.s);
        CHECK(onBlack.v == 0.0f);

        // White: hue stays, saturation 0. Keeping s at v = 1 would give a vivid color:
        // measured 0.94 away from white in linear with s = 0.8.
        const ImOk::Okhsv onWhite{ ImOk::Internal::OkLabToOkhsvKeeping(white, current) };
        CHECK(onWhite.h == current.h);
        CHECK(onWhite.s == 0.0f);

        // A chromatic color replaces everything
        const ImOk::Okhsv onOrange{ ImOk::Internal::OkLabToOkhsvKeeping(orange, current) };
        const ImOk::Okhsv expected{ ImOk::OkLabToOkhsv(orange) };
        CHECK(onOrange.h == expected.h);
        CHECK(onOrange.s == expected.s);
        CHECK(onOrange.v == expected.v);
    }

    SUBCASE("Okhsl")
    {
        const ImOk::Okhsl current{ 250.0f, 0.8f, 0.6f };

        const ImOk::Okhsl onGray{ ImOk::Internal::OkLabToOkhslKeeping(gray, current) };
        CHECK(onGray.h == current.h);
        CHECK(onGray.s == 0.0f);

        // Black and white: both keep hue and saturation (l snaps to the endpoint for any s)
        const ImOk::Okhsl onBlack{ ImOk::Internal::OkLabToOkhslKeeping(black, current) };
        CHECK(onBlack.h == current.h);
        CHECK(onBlack.s == current.s);
        CHECK(onBlack.l == 0.0f);

        const ImOk::Okhsl onWhite{ ImOk::Internal::OkLabToOkhslKeeping(white, current) };
        CHECK(onWhite.h == current.h);
        CHECK(onWhite.s == current.s);
        const ImOk::LinearSrgb shown{ ImOk::OkLabToLinearSrgb(ImOk::OkhslToOkLab(onWhite)) };
        CHECK(shown.r == doctest::Approx(1.0f).epsilon(1e-6));
        CHECK(shown.g == doctest::Approx(1.0f).epsilon(1e-6));
        CHECK(shown.b == doctest::Approx(1.0f).epsilon(1e-6));

        const ImOk::Okhsl onOrange{ ImOk::Internal::OkLabToOkhslKeeping(orange, current) };
        const ImOk::Okhsl expected{ ImOk::OkLabToOkhsl(orange) };
        CHECK(onOrange.h == expected.h);
        CHECK(onOrange.s == expected.s);
        CHECK(onOrange.l == expected.l);
    }
}