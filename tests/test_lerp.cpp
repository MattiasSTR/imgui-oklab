#include <doctest/doctest.h>
#include <cmath>
#include "imok_color.h"
#include "test_helpers.h"

using doctest::Approx;

using ImOkTest::HueDistance;

TEST_CASE("Lerp is exact at the endpoints")
{
    const ImOk::LinearSrgb linearFrom{ 0.1f, 0.2f, 0.3f };
    const ImOk::LinearSrgb linearTo{ 0.7f, 0.6f, 0.9f };
    CHECK(ImOk::LerpLinearSrgb(linearFrom, linearTo, 0.0f).g == linearFrom.g);
    CHECK(ImOk::LerpLinearSrgb(linearFrom, linearTo, 1.0f).g == linearTo.g);

    const ImOk::OkLab labFrom{ 0.3f, 0.1f, -0.05f };
    const ImOk::OkLab labTo{ 0.8f, -0.1f, 0.12f };
    CHECK(ImOk::LerpOkLab(labFrom, labTo, 0.0f).a == labFrom.a);
    CHECK(ImOk::LerpOkLab(labFrom, labTo, 1.0f).a == labTo.a);
}

TEST_CASE("Lerp: black to white midpoint depends on the space")
{
    // Linear sRGB: half the light, 0.735 encoded
    const ImOk::LinearSrgb linearMid{ ImOk::LerpLinearSrgb({ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, 0.5f) };
    CHECK(linearMid.r == 0.5f);
    CHECK(ImOk::LinearSrgbToEncodedSrgb(linearMid).r == Approx(0.735357f).epsilon(1e-5));

    // Oklab: perceptual middle gray, L = 0.5
    const ImOk::OkLab black{ ImOk::LinearSrgbToOkLab({ 0.0f, 0.0f, 0.0f }) };
    const ImOk::OkLab white{ ImOk::LinearSrgbToOkLab({ 1.0f, 1.0f, 1.0f }) };
    CHECK(ImOk::LerpOkLab(black, white, 0.5f).L == Approx(0.5f).epsilon(1e-4));
}

TEST_CASE("Lerp does not clamp t")
{
    const ImOk::LinearSrgb extrapolated{ ImOk::LerpLinearSrgb({ 0.2f, 0.2f, 0.2f }, { 0.4f, 0.4f, 0.4f }, 2.0f) };
    CHECK(extrapolated.r == Approx(0.6f).epsilon(1e-6));
}

TEST_CASE("LerpOkLCh: hue directions")
{
    struct Case
    {
        float hueA{ 0.0f };
        float hueB{ 0.0f };
        ImOk::HueDirection direction{ ImOk::HueDirection::Shorter };
        float expectedMid{ 0.0f };
    };

    const Case cases[]{
        { 350.0f,  10.0f, ImOk::HueDirection::Shorter,      0.0f }, // Across 0: 20 degrees
        { 350.0f,  10.0f, ImOk::HueDirection::Longer,     180.0f }, // The other way: 340 degrees
        { 350.0f,  10.0f, ImOk::HueDirection::Increasing,   0.0f }, // 350 -> 370
        { 350.0f,  10.0f, ImOk::HueDirection::Decreasing, 180.0f }, // 350 -> 10, going down
        {  10.0f, 350.0f, ImOk::HueDirection::Increasing, 180.0f }, // 10 -> 350, going up
        {  10.0f, 350.0f, ImOk::HueDirection::Decreasing,   0.0f }, // 10 -> -10
        {  90.0f,  90.0f, ImOk::HueDirection::Longer,     270.0f }, // Equal hues: full circle (CSS)
    };

    for (const Case& c : cases)
    {
        CAPTURE(c.hueA);
        CAPTURE(c.hueB);
        CAPTURE(static_cast<int>(c.direction));

        const ImOk::OkLCh mid{ ImOk::LerpOkLCh({ 0.6f, 0.1f, c.hueA }, { 0.6f, 0.1f, c.hueB }, 0.5f, c.direction) };
        CHECK(HueDistance(mid.h, c.expectedMid) < 1e-3f);
        CHECK(mid.h >= 0.0f);
        CHECK(mid.h < 360.0f);
    }
}

TEST_CASE("LerpOkLCh: a gray endpoint borrows the other endpoint's hue")
{
    const ImOk::OkLCh red{ ImOk::OkLabToOkLCh(ImOk::LinearSrgbToOkLab({ 1.0f, 0.0f, 0.0f })) };
    const ImOk::OkLCh gray{ 0.6f, 0.0f, 0.0f }; // Hue 0 by convention, meaningless

    const float ts[]{ 0.25f, 0.5f, 0.75f };
    for (const float t : ts)
    {
        CAPTURE(t);
        CHECK(HueDistance(ImOk::LerpOkLCh(red, gray, t, ImOk::HueDirection::Shorter).h, red.h) < 1e-3f);
        CHECK(HueDistance(ImOk::LerpOkLCh(gray, red, t, ImOk::HueDirection::Shorter).h, red.h) < 1e-3f);
    }
}