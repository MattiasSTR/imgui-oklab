#include <doctest/doctest.h>
#include "imok_color.h"

using doctest::Approx;

TEST_CASE("OkLCh: sRGB red matches published value")
{
    const ImOk::OkLCh lch{ ImOk::OkLabToOkLCh(ImOk::LinearSrgbToOkLab({ 1.0f, 0.0f, 0.0f })) };
    CHECK(lch.L == Approx(0.627955f).epsilon(1e-3));
    CHECK(lch.C == Approx(0.257683f).epsilon(1e-3));
    CHECK(lch.h == Approx(29.2339f).epsilon(1e-3));
}

TEST_CASE("OkLCh: grays get hue 0, chroma untouched")
{
    const ImOk::OkLCh lch{ ImOk::OkLabToOkLCh({ 0.5f, 1e-7f, -1e-7f }) };
    CHECK(lch.h == 0.0f);
    CHECK(lch.C > 0.0f); // tiny, but not snapped to zero
}

TEST_CASE("OkLCh: hue never reaches 360")
{
    // atan2 gives a tiny negative angle here; naive +360 rounds to exactly 360.0f.
    const ImOk::OkLCh lch{ ImOk::OkLabToOkLCh({ 0.5f, 0.1f, -1e-9f }) };
    CHECK(lch.h >= 0.0f);
    CHECK(lch.h < 360.0f);
}

TEST_CASE("OkLCh: hue wraps on the way back")
{
    const ImOk::OkLab a{ ImOk::OkLChToOkLab({ 0.5f, 0.1f, 0.0f }) };
    const ImOk::OkLab b{ ImOk::OkLChToOkLab({ 0.5f, 0.1f, 360.0f }) };
    const ImOk::OkLab c{ ImOk::OkLChToOkLab({ 0.5f, 0.1f, -90.0f }) };
    const ImOk::OkLab d{ ImOk::OkLChToOkLab({ 0.5f, 0.1f, 270.0f }) };
    CHECK(a.a == Approx(b.a).epsilon(1e-5));
    CHECK(a.b == Approx(b.b).epsilon(1e-5));
    CHECK(c.a == Approx(d.a).epsilon(1e-5));
    CHECK(c.b == Approx(d.b).epsilon(1e-5));
}

TEST_CASE("OkLCh: round-trips")
{
    for (int ia{ -4 }; ia <= 4; ++ia)
    {
        for (int ib{ -4 }; ib <= 4; ++ib)
        {
            const ImOk::OkLab lab{ 0.6f, ia * 0.1f, ib * 0.1f };
            const ImOk::OkLab back{ ImOk::OkLChToOkLab(ImOk::OkLabToOkLCh(lab)) };
            CHECK(back.L == Approx(lab.L).epsilon(1e-5));
            CHECK(back.a == Approx(lab.a).epsilon(1e-5));
            CHECK(back.b == Approx(lab.b).epsilon(1e-5));
        }
    }
}