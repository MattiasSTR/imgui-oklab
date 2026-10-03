#include <doctest/doctest.h>
#include "imok_internal.h"

using ImOk::Internal::EncodedSrgbToStored;
using ImOk::Internal::LinearSrgbToStored;
using ImOk::Internal::StoredToEncodedSrgb;
using ImOk::Internal::StoredToLinearSrgb;
using ImOk::Internal::RaiseStoredToLargestChannel;

TEST_CASE("Reading storage in its own space is a bit-exact copy")
{
    const float col[3]{ 0.123456789f, 0.5f, 0.987654321f };
    const ImOk::EncodedSrgb encoded{ StoredToEncodedSrgb(col, ImOkStoredAs_EncodedSrgb) };
    const ImOk::LinearSrgb linear{ StoredToLinearSrgb(col, ImOkStoredAs_LinearSrgb) };
    CHECK(encoded.r == col[0]);
    CHECK(encoded.b == col[2]);
    CHECK(linear.r == col[0]);
    CHECK(linear.b == col[2]);
}

TEST_CASE("Every byte enters EncodedSrgb storage bit-exact, LinearSrgb storage decoded")
{
    for (int k{ 0 }; k <= 255; ++k)
    {
        const float v{ static_cast<float>(k) / 255.0f };
        float encodedCol[3]{};
        float linearCol[3]{};
        EncodedSrgbToStored({ v, v, v }, ImOkStoredAs_EncodedSrgb, encodedCol);
        EncodedSrgbToStored({ v, v, v }, ImOkStoredAs_LinearSrgb, linearCol);
        REQUIRE(encodedCol[0] == v);
        REQUIRE(linearCol[0] == ImOk::EncodedSrgbToLinearSrgb({ v, v, v }).r);
    }
}

TEST_CASE("Linear into storage: white is exactly 1, overshoot is clamped")
{
    float col[3]{};
    LinearSrgbToStored({ 1.0f, 1.0f, 1.0f }, ImOkStoredAs_EncodedSrgb, col);
    CHECK(col[0] == 1.0f);

    LinearSrgbToStored({ 1.0005f, -0.0005f, 0.5f }, ImOkStoredAs_LinearSrgb, col);
    CHECK(col[0] == 1.0f);
    CHECK(col[1] == 0.0f);
    CHECK(col[2] == 0.5f);
}

TEST_CASE("NoBrightness raise: largest linear channel exactly 1, chromaticity kept")
{
    float col[3]{ 0.2f, 0.1f, 0.05f };
    CHECK(RaiseStoredToLargestChannel(col, ImOkStoredAs_LinearSrgb));
    CHECK(col[0] == 1.0f);
    CHECK(col[1] == doctest::Approx(0.5f).epsilon(1e-6));
    CHECK(col[2] == doctest::Approx(0.25f).epsilon(1e-6));
}

TEST_CASE("NoBrightness raise in EncodedSrgb storage: decoded, scaled, encoded again")
{
    float col[3]{ 0.5f, 0.25f, 0.125f };
    const ImOk::LinearSrgb before{ StoredToLinearSrgb(col, ImOkStoredAs_EncodedSrgb) };
    CHECK(RaiseStoredToLargestChannel(col, ImOkStoredAs_EncodedSrgb));
    const ImOk::LinearSrgb after{ StoredToLinearSrgb(col, ImOkStoredAs_EncodedSrgb) };
    CHECK(col[0] == 1.0f); // Encoded 1 is linear 1
    CHECK(after.g / after.r == doctest::Approx(before.g / before.r).epsilon(1e-5));
    CHECK(after.b / after.r == doctest::Approx(before.b / before.r).epsilon(1e-5));
}

TEST_CASE("NoBrightness raise: a col already at 1 is left bit-exact")
{
    const float original[3]{ 1.0f, 0.123456789f, 0.5f };
    const ImOkStoredAs storages[]{ ImOkStoredAs_EncodedSrgb, ImOkStoredAs_LinearSrgb };
    for (const ImOkStoredAs storage : storages)
    {
        float col[3]{ original[0], original[1], original[2] };
        CHECK_FALSE(RaiseStoredToLargestChannel(col, storage));
        CHECK(col[0] == original[0]);
        CHECK(col[1] == original[1]);
        CHECK(col[2] == original[2]);
    }
}

TEST_CASE("NoBrightness raise: black has no chromaticity and becomes white")
{
    float col[3]{ 0.0f, 0.0f, 0.0f };
    CHECK(RaiseStoredToLargestChannel(col, ImOkStoredAs_LinearSrgb));
    CHECK(col[0] == 1.0f);
    CHECK(col[1] == 1.0f);
    CHECK(col[2] == 1.0f);
}