#include <doctest/doctest.h>
#include "imok_color.h"

using doctest::Approx;

namespace
{
    void CheckOkLab(const ImOk::OkLab& actual, float L, float a, float b)
    {
        CHECK(actual.L == Approx(L).epsilon(1e-3));
        CHECK(actual.a == Approx(a).epsilon(1e-3));
        CHECK(actual.b == Approx(b).epsilon(1e-3));
    }
}

TEST_CASE("Oklab: white and black")
{
    CheckOkLab(ImOk::LinearSrgbToOkLab({ 1.0f, 1.0f, 1.0f }), 1.0f, 0.0f, 0.0f);
    CheckOkLab(ImOk::LinearSrgbToOkLab({ 0.0f, 0.0f, 0.0f }), 0.0f, 0.0f, 0.0f);
}

TEST_CASE("Oklab: grays have no chroma")
{
    for (int i{ 1 }; i <= 10; ++i)
    {
        const float v{ i / 10.0f };
        const ImOk::OkLab lab{ ImOk::LinearSrgbToOkLab({ v, v, v }) };
        CHECK(lab.a == Approx(0.0f).epsilon(1e-4));
        CHECK(lab.b == Approx(0.0f).epsilon(1e-4));
    }
}

TEST_CASE("Oklab: sRGB primaries")
{
    CheckOkLab(ImOk::LinearSrgbToOkLab({ 1.0f, 0.0f, 0.0f }), 0.627955f,  0.224863f,  0.125846f);
    CheckOkLab(ImOk::LinearSrgbToOkLab({ 0.0f, 1.0f, 0.0f }), 0.866440f, -0.233888f,  0.179498f);
    CheckOkLab(ImOk::LinearSrgbToOkLab({ 0.0f, 0.0f, 1.0f }), 0.452014f, -0.032457f, -0.311528f);
}

TEST_CASE("Oklab: round-trips, including out-of-range values")
{
    const float values[]{ -0.2f, 0.0f, 0.1f, 0.5f, 0.9f, 1.0f, 1.5f };
    for (float r : values)
    {
        for (float g : values)
        {
            for (float b : values)
            {
                const ImOk::LinearSrgb back{ ImOk::OkLabToLinearSrgb(ImOk::LinearSrgbToOkLab({ r, g, b })) };
                CHECK(back.r == Approx(r).epsilon(1e-4));
                CHECK(back.g == Approx(g).epsilon(1e-4));
                CHECK(back.b == Approx(b).epsilon(1e-4));
            }
        }
    }
}