#include <doctest/doctest.h>
#include "imok_color.h"

using doctest::Approx;

TEST_CASE("sRGB transfer function: endpoints map exactly")
{
    CHECK(ImOk::EncodedSrgbToLinearSrgb({ 0.0f, 0.0f, 0.0f }).r == 0.0f);
    CHECK(ImOk::EncodedSrgbToLinearSrgb({ 1.0f, 1.0f, 1.0f }).r == Approx(1.0f).epsilon(1e-6));
    CHECK(ImOk::LinearSrgbToEncodedSrgb({ 1.0f, 1.0f, 1.0f }).r == 1.0f); // Exact: white stays 1.0
}

TEST_CASE("sRGB transfer function: known values")
{
    // Half the light is 0.735 encoded, not 0.5
    CHECK(ImOk::LinearSrgbToEncodedSrgb({ 0.5f, 0.5f, 0.5f }).r == Approx(0.735357f).epsilon(1e-5));
    CHECK(ImOk::EncodedSrgbToLinearSrgb({ 0.5f, 0.5f, 0.5f }).r == Approx(0.214041f).epsilon(1e-5));
}

TEST_CASE("sRGB transfer function: round-trips across the range")
{
    for (int i{ 0 }; i <= 1000; ++i)
    {
        const float v{ i / 1000.0f };
        const ImOk::EncodedSrgb back{ ImOk::LinearSrgbToEncodedSrgb(ImOk::EncodedSrgbToLinearSrgb({ v, v, v })) };
        CHECK(back.r == Approx(v).epsilon(1e-5));
    }
}

TEST_CASE("sRGB transfer function: does not clamp out-of-range values")
{
    CHECK(ImOk::LinearSrgbToEncodedSrgb({ 2.0f, 0.0f, 0.0f }).r > 1.0f);          // HDR
    CHECK(ImOk::LinearSrgbToEncodedSrgb({ -0.5f, 0.0f, 0.0f }).r < 0.0f);         // out of gamut
    CHECK(ImOk::LinearSrgbToEncodedSrgb({ -0.5f, 0.0f, 0.0f }).r ==
          Approx(-ImOk::LinearSrgbToEncodedSrgb({ 0.5f, 0.0f, 0.0f }).r));         // sign-preserving
}