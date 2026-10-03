#include <doctest/doctest.h>
#include <cmath>
#include "imok_color_internal.h"
#include "test_helpers.h"

using doctest::Approx;

namespace
{
    using ImOk::Internal::DEG_TO_RAD;

    // Ottosson's compute_max_saturation as he wrote it, region selection included, to pin ImOk's
    // one DEVIATION (blue's corner, see ComputeMaxSaturation). Split in two so the corner's
    // expected result, the green region, can be computed on its own.
    struct Region
    {
        float k0{ 0.0f }, k1{ 0.0f }, k2{ 0.0f }, k3{ 0.0f }, k4{ 0.0f };
        float wl{ 0.0f }, wm{ 0.0f }, ws{ 0.0f };
    };

    constexpr Region OTTOSSON_RED{ +1.19086277f, +1.76576728f, +0.59662641f, +0.75515197f, +0.56771245f,
                                   +4.0767416621f, -3.3077115913f, +0.2309699292f };
    constexpr Region OTTOSSON_GREEN{ +0.73956515f, -0.45954404f, +0.08285427f, +0.12541070f, +0.14503204f,
                                     -1.2684380046f, +2.6097574011f, -0.3413193965f };
    constexpr Region OTTOSSON_BLUE{ +1.35733652f, -0.00915799f, -1.15130210f, -0.50559606f, +0.00692167f,
                                    -0.0041960863f, -0.7034186147f, +1.7076147010f };

    float OttossonMaxSaturationIn(const Region& k, float a, float b)
    {
        float S{ k.k0 + k.k1 * a + k.k2 * b + k.k3 * a * a + k.k4 * a * b };

        const float k_l{ +0.3963377774f * a + 0.2158037573f * b };
        const float k_m{ -0.1055613458f * a - 0.0638541728f * b };
        const float k_s{ -0.0894841775f * a - 1.2914855480f * b };

        const float l_{ 1.0f + S * k_l };
        const float m_{ 1.0f + S * k_m };
        const float s_{ 1.0f + S * k_s };

        const float l{ l_ * l_ * l_ };
        const float m{ m_ * m_ * m_ };
        const float s{ s_ * s_ * s_ };

        const float l_dS{ 3.0f * k_l * l_ * l_ };
        const float m_dS{ 3.0f * k_m * m_ * m_ };
        const float s_dS{ 3.0f * k_s * s_ * s_ };

        const float l_dS2{ 6.0f * k_l * k_l * l_ };
        const float m_dS2{ 6.0f * k_m * k_m * m_ };
        const float s_dS2{ 6.0f * k_s * k_s * s_ };

        const float f{ k.wl * l + k.wm * m + k.ws * s };
        const float f1{ k.wl * l_dS + k.wm * m_dS + k.ws * s_dS };
        const float f2{ k.wl * l_dS2 + k.wm * m_dS2 + k.ws * s_dS2 };

        S = S - f * f1 / (f1 * f1 - 0.5f * f * f2);
        return S;
    }

    float OttossonMaxSaturation(float a, float b)
    {
        if (-1.88170328f * a - 0.80936493f * b > 1.0f)
        {
            return OttossonMaxSaturationIn(OTTOSSON_RED, a, b);
        }
        if (1.81444104f * a - 1.19445276f * b > 1.0f)
        {
            return OttossonMaxSaturationIn(OTTOSSON_GREEN, a, b);
        }
        return OttossonMaxSaturationIn(OTTOSSON_BLUE, a, b);
    }

    using ImOkTest::GamutBoundaryError;
}

TEST_CASE("Cusp at the hue of each cube corner is that corner")
{
    // The six saturated cube corners: the kinks in the cusp curve, where the max-saturation
    // estimate is least accurate. Blue's rounding neighbors are tested below.
    const ImOk::LinearSrgb corners[]{
        { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f },    // red, green, blue
        { 1.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 1.0f }, { 1.0f, 0.0f, 1.0f } };  // secondaries

    for (const ImOk::LinearSrgb& corner : corners)
    {
        CAPTURE(corner.r);
        CAPTURE(corner.g);
        CAPTURE(corner.b);

        const ImOk::OkLab lab{ ImOk::LinearSrgbToOkLab(corner) };
        const float C{ std::sqrt(lab.a * lab.a + lab.b * lab.b) };
        const ImOk::Internal::LC cusp{ ImOk::Internal::FindCusp(lab.a / C, lab.b / C) };

        CHECK(cusp.L == Approx(lab.L).epsilon(1e-3));
        CHECK(cusp.C == Approx(C).epsilon(1e-3));
    }
}

// Every pure blue lands on blue's corner, where both region tests are 1 up to rounding, and its
// a / C and its hue's cos, sin differ by a few ulps. One ulp from a CRT's cbrtf (MSVC's) or from
// FMA contraction used to choose the region. Now neither may, on any compiler.
TEST_CASE("Cusp at the hue of every pure blue is pure blue, from either direction")
{
    const ImOk::OkLab blue{ ImOk::LinearSrgbToOkLab({ 0.0f, 0.0f, 1.0f }) };
    const float blueC{ std::sqrt(blue.a * blue.a + blue.b * blue.b) };

    for (int byte{ 1 }; byte <= 255; ++byte)
    {
        CAPTURE(byte);
        const float encoded{ static_cast<float>(byte) / 255.0f };
        const ImOk::OkLab lab{ ImOk::LinearSrgbToOkLab(ImOk::EncodedSrgbToLinearSrgb({ 0.0f, 0.0f, encoded })) };
        const float C{ std::sqrt(lab.a * lab.a + lab.b * lab.b) };
        const float hRad{ ImOk::OkLabToOkLCh(lab).h * DEG_TO_RAD };

        const ImOk::Internal::LC fromLab{ ImOk::Internal::FindCusp(lab.a / C, lab.b / C) };
        const ImOk::Internal::LC fromHue{ ImOk::Internal::FindCusp(std::cos(hRad), std::sin(hRad)) };

        CHECK(fromLab.L == Approx(blue.L).epsilon(1e-3));
        CHECK(fromLab.C == Approx(blueC).epsilon(1e-3));
        CHECK(fromHue.L == Approx(blue.L).epsilon(1e-3));
        CHECK(fromHue.C == Approx(blueC).epsilon(1e-3));
    }
}

TEST_CASE("Max saturation is Ottosson's except at blue's corner, which takes the green region")
{
    const float blueHue{ ImOk::OkLabToOkLCh(ImOk::LinearSrgbToOkLab({ 0.0f, 0.0f, 1.0f })).h };

    // Every 0.001 degrees outside the corner (about 3e-4 degrees on each side): his result
    int differing{ 0 };
    float firstDifferingHue{ -1.0f };
    for (int i{ 0 }; i < 360000; ++i)
    {
        const float deg{ static_cast<float>(i) * 0.001f };
        if (std::fabs(deg - blueHue) < 4e-4f)
        {
            continue;
        }
        const float hRad{ deg * DEG_TO_RAD };
        const float a{ std::cos(hRad) };
        const float b{ std::sin(hRad) };
        if (ImOk::Internal::ComputeMaxSaturation(a, b) != Approx(OttossonMaxSaturation(a, b)).epsilon(1e-6))
        {
            ++differing;
            firstDifferingHue = (firstDifferingHue < 0.0f) ? deg : firstDifferingHue;
        }
    }
    INFO("first differing hue: ", firstDifferingHue);
    CHECK(differing == 0);

    // Every float hue within 10 ulps of blue's: the green region, whichever his tests pick
    float hue{ blueHue };
    for (int i{ 0 }; i < 10; ++i)
    {
        hue = std::nextafter(hue, 0.0f);
    }
    for (int i{ 0 }; i <= 20; ++i)
    {
        CAPTURE(hue);
        const float hRad{ hue * DEG_TO_RAD };
        const float a{ std::cos(hRad) };
        const float b{ std::sin(hRad) };
        CHECK(ImOk::Internal::ComputeMaxSaturation(a, b) == Approx(OttossonMaxSaturationIn(OTTOSSON_GREEN, a, b)).epsilon(1e-6));
        hue = std::nextafter(hue, 360.0f);
    }
}

TEST_CASE("Cusp lies on the sRGB gamut edge for every hue")
{
    float worstError{ 0.0f };
    float worstHue{ 0.0f };

    for (int i{ 0 }; i < 3600; ++i)
    {
        const float deg{ i * 0.1f };
        const float hRad{ deg * DEG_TO_RAD };
        const float a{ std::cos(hRad) };
        const float b{ std::sin(hRad) };

        const ImOk::Internal::LC cusp{ ImOk::Internal::FindCusp(a, b) };
        const ImOk::LinearSrgb rgb{ ImOk::OkLabToLinearSrgb({ cusp.L, cusp.C * a, cusp.C * b }) };

        // On the gamut edge: brightest channel exactly 1, darkest exactly 0
        const float maxError{ std::fabs(std::fmax(std::fmax(rgb.r, rgb.g), rgb.b) - 1.0f) };
        const float minError{ std::fabs(std::fmin(std::fmin(rgb.r, rgb.g), rgb.b)) };
        const float error{ std::fmax(maxError, minError) };

        if (error > worstError)
        {
            worstError = error;
            worstHue = deg;
        }
    }

    MESSAGE("Worst cusp error: ", worstError, " at hue ", worstHue);
    // ~5e-4 near blue at 0.1-degree sampling; the exact corner has its own test
    CHECK(worstError < 1e-3f);
}

TEST_CASE("Gamut intersection lies on the sRGB boundary for every hue")
{
    struct Line
    {
        const char* name{ "" };
        float L0{ 0.0f };
        float L1{ 0.0f };
        float C1{ 0.0f };
    };

    const Line lines[]{
        { "horizontal at L = 0.5", 0.5f, 0.5f, 1.0f }, // Constant lightness, as Okhsl uses it
        { "towards upper half",    0.5f, 0.9f, 0.5f }, // Upper half: Halley branch
        { "towards lower half",    0.5f, 0.1f, 0.5f }, // Lower half: exact triangle branch
    };

    // Blue's corner dominates the error, so it is measured apart
    constexpr float BLUE_HUE{ 264.052f };
    constexpr float BLUE_WINDOW{ 1.0f }; // degrees on each side

    for (const Line& line : lines)
    {
        const doctest::String name{ line.name }; // A raw const char* member prints as a bool
        CAPTURE(name);

        float worstError{ 0.0f };
        float worstHue{ 0.0f };
        float worstErrorNearBlue{ 0.0f };

        for (int i{ 0 }; i < 3600; ++i)
        {
            const float deg{ i * 0.1f };
            const float hRad{ deg * DEG_TO_RAD };
            const float a{ std::cos(hRad) };
            const float b{ std::sin(hRad) };

            const float t{ ImOk::Internal::FindGamutIntersection(a, b, line.L1, line.C1, line.L0) };
            const float L{ line.L0 * (1.0f - t) + t * line.L1 };
            const float C{ t * line.C1 };

            const float error{ GamutBoundaryError(ImOk::OkLabToLinearSrgb({ L, C * a, C * b })) };

            if (std::fabs(deg - BLUE_HUE) < BLUE_WINDOW)
            {
                worstErrorNearBlue = std::fmax(worstErrorNearBlue, error);
            }
            else if (error > worstError)
            {
                worstError = error;
                worstHue = deg;
            }
        }

        MESSAGE(name, ": worst error ", worstError, " at hue ", worstHue,
                ", near blue ", worstErrorNearBlue);
        // Measured ~2.4e-4 away from blue (horizontal line, ~300 degrees), the same in double:
        // Ottosson's one-step estimate. Always slightly inside the gamut.
        CHECK(worstError < 5e-4f);
        CHECK(worstErrorNearBlue < 1e-3f); // Blue's corner, see FindCusp
    }
}

TEST_CASE("Gamut intersection with the precomputed cusp matches the plain overload")
{
    const float hRad{ 45.0f * DEG_TO_RAD };
    const float a{ std::cos(hRad) };
    const float b{ std::sin(hRad) };

    const ImOk::Internal::LC cusp{ ImOk::Internal::FindCusp(a, b) };
    CHECK(ImOk::Internal::FindGamutIntersection(a, b, 0.5f, 1.0f, 0.5f, cusp) ==
          ImOk::Internal::FindGamutIntersection(a, b, 0.5f, 1.0f, 0.5f));
}