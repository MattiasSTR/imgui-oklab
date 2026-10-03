#include <doctest/doctest.h>
#include "imok_color.h"

using doctest::Approx;

TEST_CASE("Toe maps black and white to themselves")
{
    CHECK(ImOk::Toe(0.0f) == Approx(0.0f).epsilon(1e-6));
    CHECK(ImOk::Toe(1.0f) == Approx(1.0f).epsilon(1e-4));
}

TEST_CASE("Toe: 50% Lr matches Ottosson's published luminance")
{
    // Per Ottosson: Lr = 0.5 corresponds to luminance Y = 0.18419.
    // For a gray, linear sRGB equals Y.
    const float Y{ 0.18419f };
    const ImOk::OkLab lab{ ImOk::LinearSrgbToOkLab({ Y, Y, Y }) };
    CHECK(ImOk::Toe(lab.L) == Approx(0.5f).epsilon(1e-3));
}

TEST_CASE("Toe round-trips and is monotonic")
{
    float previous{ -1.0f };
    for (int i{ 0 }; i <= 1000; ++i)
    {
        const float L{ i / 1000.0f };
        const float Lr{ ImOk::Toe(L) };

        CHECK(ImOk::ToeInv(Lr) == Approx(L).epsilon(1e-5));
        CHECK(Lr > previous);
        previous = Lr;
    }
}