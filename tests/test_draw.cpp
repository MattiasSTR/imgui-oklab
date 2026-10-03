#include <doctest/doctest.h>
#include <cmath>
#include <algorithm>
#include <vector>
#include "imok_color.h"
#include "imok_internal.h"

namespace
{
    // MIRRORS imok.cpp: keep in sync with SURFACE_CELL_SIZE, the triangle split in
    // AddRectSurface, DISPLAY_CLAMP_TOLERANCE, and the vertex color path of LinearSrgbToImU32
    // (clamp if near the gamut, else clip with AdaptiveMidGray; encode, clamp, round to
    // nearest). If those change, this test measures the wrong thing.
    constexpr float SURFACE_CELL_SIZE{ 2.0f };

    // An 8-bit color, held as floats (0..255) so the GPU interpolation can be simulated
    struct Rgb8
    {
        float r{ 0.0f };
        float g{ 0.0f };
        float b{ 0.0f };
    };

    float ToByte(float x)
    {
        return std::floor(std::fmin(std::fmax(x, 0.0f), 1.0f) * 255.0f + 0.5f);
    }

    constexpr float DISPLAY_CLAMP_TOLERANCE{ 0.001f };

    // What LinearSrgbToImU32 produces, and what an exact per-pixel surface would show
    Rgb8 ToRgb8(const ImOk::LinearSrgb& c)
    {
        const float low{ -DISPLAY_CLAMP_TOLERANCE };
        const float high{ 1.0f + DISPLAY_CLAMP_TOLERANCE };
        const bool nearGamut{ c.r >= low && c.r <= high && c.g >= low && c.g <= high && c.b >= low && c.b <= high };
        const ImOk::LinearSrgb clipped{ nearGamut ? c : ImOk::ClipToSrgbGamut(c, ImOk::GamutClipMethod::AdaptiveMidGray) };
        const ImOk::EncodedSrgb encoded{ ImOk::LinearSrgbToEncodedSrgb(clipped) };
        return { ToByte(encoded.r), ToByte(encoded.g), ToByte(encoded.b) };
    }

    // A picker square's color at (u, v) in [0, 1] at a hue: u left to right, v top to bottom
    typedef ImOk::LinearSrgb (*SquareColorFn)(float hue, float u, float v);

    // Same mapping as OkhsvSaturationValueColorAt: u is saturation, v runs top to bottom
    ImOk::LinearSrgb OkhsvSquareColor(float hue, float u, float v)
    {
        return ImOk::OkLabToLinearSrgb(ImOk::OkhsvToOkLab({ hue, u, 1.0f - v }));
    }

    // Same mapping as OkhslSaturationLightnessColorAt
    ImOk::LinearSrgb OkhslSquareColor(float hue, float u, float v)
    {
        return ImOk::OkLabToLinearSrgb(ImOk::OkhslToOkLab({ hue, u, 1.0f - v }));
    }

    // GPU interpolation inside one cell. Same split as AddRectSurface: triangles
    // (top-left, top-right, bottom-right) and (top-left, bottom-right, bottom-left).
    float Interpolate(float topLeft, float topRight, float bottomLeft, float bottomRight, float tx, float ty)
    {
        return (tx >= ty)
            ? topLeft + (topRight - topLeft) * tx + (bottomRight - topRight) * ty
            : topLeft + (bottomLeft - topLeft) * ty + (bottomRight - bottomLeft) * tx;
    }

    // The worst difference, in 8-bit steps, between a picker square at a hue drawn `size` px
    // wide and its exact colors at every pixel center
    float WorstSquareDiff(SquareColorFn colorAt, float hue, int size)
    {
        const int cells{ static_cast<int>(std::ceil(size / SURFACE_CELL_SIZE)) };
        const int columns{ cells + 1 };
        std::vector<Rgb8> vertices(static_cast<size_t>(columns * columns)); // Parentheses: size, not a list

        // Vertex colors, in the same layout as AddRectSurface
        for (int y{ 0 }; y <= cells; ++y)
        {
            for (int x{ 0 }; x <= cells; ++x)
            {
                const float u{ static_cast<float>(x) / cells };
                const float v{ static_cast<float>(y) / cells };
                vertices[static_cast<size_t>(y * columns + x)] = ToRgb8(colorAt(hue, u, v));
            }
        }

        // Every pixel center: GPU result vs exact color
        float worstDiff{ 0.0f };
        for (int py{ 0 }; py < size; ++py)
        {
            for (int px{ 0 }; px < size; ++px)
            {
                const float u{ (px + 0.5f) / size };
                const float v{ (py + 0.5f) / size };

                const float fx{ u * cells };
                const float fy{ v * cells };
                const int ix{ std::min(static_cast<int>(fx), cells - 1) };
                const int iy{ std::min(static_cast<int>(fy), cells - 1) };
                const float tx{ fx - ix };
                const float ty{ fy - iy };

                const Rgb8& topLeft{ vertices[static_cast<size_t>(iy * columns + ix)] };
                const Rgb8& topRight{ vertices[static_cast<size_t>(iy * columns + ix + 1)] };
                const Rgb8& bottomLeft{ vertices[static_cast<size_t>((iy + 1) * columns + ix)] };
                const Rgb8& bottomRight{ vertices[static_cast<size_t>((iy + 1) * columns + ix + 1)] };

                // The GPU writes the interpolated value to an 8-bit target, rounding to nearest
                const Rgb8 drawn{
                    std::floor(Interpolate(topLeft.r, topRight.r, bottomLeft.r, bottomRight.r, tx, ty) + 0.5f),
                    std::floor(Interpolate(topLeft.g, topRight.g, bottomLeft.g, bottomRight.g, tx, ty) + 0.5f),
                    std::floor(Interpolate(topLeft.b, topRight.b, bottomLeft.b, bottomRight.b, tx, ty) + 0.5f) };

                const Rgb8 exact{ ToRgb8(colorAt(hue, u, v)) };

                worstDiff = std::fmax(worstDiff, std::fmax(std::fabs(drawn.r - exact.r),
                                                 std::fmax(std::fabs(drawn.g - exact.g), std::fabs(drawn.b - exact.b))));
            }
        }
        return worstDiff;
    }
}

TEST_CASE("Okhsv square mesh stays within 1 8-bit step of the exact colors")
{
    // 256 px, as a typical picker. With 2 px cells the worst difference is 1 step, the same
    // as 8-bit rounding itself: the mesh is as exact as 8-bit output can show.
    constexpr int SIZE{ 256 };
    constexpr int HUE_STEPS{ 24 }; // Every 15 degrees, includes 330

    float worstDiff{ 0.0f };
    float worstHue{ 0.0f };
    for (int hueStep{ 0 }; hueStep < HUE_STEPS; ++hueStep)
    {
        const float hue{ hueStep * (360.0f / HUE_STEPS) };
        const float diff{ WorstSquareDiff(OkhsvSquareColor, hue, SIZE) };
        if (diff > worstDiff)
        {
            worstDiff = diff;
            worstHue = hue;
        }
    }

    MESSAGE("Worst mesh difference: ", worstDiff, " 8-bit steps at hue ", worstHue);
    CHECK(worstDiff <= 1.0f);
}

TEST_CASE("Squares across blue's fold: no pixel beyond the display clamp, so no hole")
{
    // From blue's hue to ~0.16 degrees above it the gamut has a gap (see FindCusp), and both
    // squares cross it near full saturation. The display clamp hides it only while every overshoot
    // stays within its tolerance; beyond, clipping moved colors here by up to 63 steps. Measured:
    // -7.8e-4 (Okhsv), -7.5e-4 (Okhsl), at 128 x 128 pixel centers.
    constexpr int SIZE{ 128 };
    const SquareColorFn squares[]{ OkhsvSquareColor, OkhslSquareColor };
    const float blueHue{ ImOk::OkLabToOkLCh(ImOk::LinearSrgbToOkLab({ 0.0f, 0.0f, 1.0f })).h };

    float lowest{ 0.0f };
    float highest{ 1.0f };
    float lowestHue{ blueHue };
    const auto measure = [&](float hue)
    {
        for (const SquareColorFn colorAt : squares)
        {
            for (int py{ 0 }; py < SIZE; ++py)
            {
                for (int px{ 0 }; px < SIZE; ++px)
                {
                    const ImOk::LinearSrgb c{ colorAt(hue, (px + 0.5f) / SIZE, (py + 0.5f) / SIZE) };
                    const float low{ std::fmin(std::fmin(c.r, c.g), c.b) };
                    if (low < lowest)
                    {
                        lowest = low;
                        lowestHue = hue;
                    }
                    highest = std::fmax(highest, std::fmax(std::fmax(c.r, c.g), c.b));
                }
            }
        }
    };

    float hue{ blueHue };
    for (int i{ 0 }; i < 10; ++i)
    {
        hue = std::nextafter(hue, 0.0f);
    }
    for (int i{ 0 }; i <= 20; ++i)
    {
        measure(hue);
        hue = std::nextafter(hue, 360.0f);
    }
    for (int i{ 0 }; i <= 40; ++i)
    {
        measure(blueHue + static_cast<float>(i) * 0.005f);
    }

    MESSAGE("Squares across blue's fold: lowest channel ", lowest, " at hue ", lowestHue, ", highest ", highest);
    CHECK(lowest >= -DISPLAY_CLAMP_TOLERANCE);
    CHECK(highest <= 1.0f + DISPLAY_CLAMP_TOLERANCE);
}

TEST_CASE("Okhsv square near blue's hue: near-gamut overshoot is clamped, not clipped")
{
    // Okhsv lands a hair outside sRGB. Within ~0.1 degrees of blue's hue, clipping that keeping
    // hue moved a patch near s = 0.94 by up to 63 steps (Oklab's hue folds there), away from the
    // per-channel clamp the widgets store. Every 0.01 degrees within 0.2 of blue's hue.
    const float blueHue{ ImOk::OkLabToOkLCh(ImOk::LinearSrgbToOkLab({ 0.0f, 0.0f, 1.0f })).h };
    for (const int size : { 256, 128, 64 })
    {
        float worstDiff{ 0.0f };
        for (int i{ -20 }; i <= 20; ++i)
        {
            worstDiff = std::fmax(worstDiff, WorstSquareDiff(OkhsvSquareColor, blueHue + static_cast<float>(i) * 0.01f, size));
        }
        INFO("size ", size);
        CHECK(worstDiff <= 5.0f);
    }
}

TEST_CASE("Okhsl square mesh: worst at the gamut's corner near s = 1")
{
    // Okhsl's s = 1 edge follows the gamut boundary, which has a corner at the cusp's lightness,
    // and near s = 1 its chroma rises steeply, so a channel leaves 0 within the last cells, where
    // the sRGB curve is steepest. Vertices interpolated in encoded bytes can't follow either. At
    // 256 px the error is at most 2 steps below s = 0.9; at 64 px it spreads (8 below s = 0.9).
    // Pinned so it can't grow. Every 15 degrees, plus every 0.01 degrees within 0.2 of blue's hue.
    struct SizeBound
    {
        int size{ 0 };
        float bound{ 0.0f };
    };
    const float blueHue{ ImOk::OkLabToOkLCh(ImOk::LinearSrgbToOkLab({ 0.0f, 0.0f, 1.0f })).h };

    for (const SizeBound& sizeBound : { SizeBound{ 256, 23.0f }, SizeBound{ 128, 16.0f }, SizeBound{ 64, 35.0f } })
    {
        float worstDiff{ 0.0f };
        float worstHue{ 0.0f };
        const auto measure = [&](float hue)
        {
            const float diff{ WorstSquareDiff(OkhslSquareColor, hue, sizeBound.size) };
            if (diff > worstDiff)
            {
                worstDiff = diff;
                worstHue = hue;
            }
        };
        for (int hueStep{ 0 }; hueStep < 24; ++hueStep)
        {
            measure(static_cast<float>(hueStep) * 15.0f);
        }
        for (int i{ -20 }; i <= 20; ++i)
        {
            measure(blueHue + static_cast<float>(i) * 0.01f);
        }

        MESSAGE("Okhsl square, ", sizeBound.size, " px: ", worstDiff, " 8-bit steps at hue ", worstHue);
        CHECK(worstDiff <= sizeBound.bound);
    }
}

namespace
{
    // MIRRORS imok.cpp: keep in sync with GRADIENT_SEGMENT_WIDTH and AddRectGradient (a strip
    // of segments, vertex i at t = i / segments), with the same vertex color path as above
    constexpr float GRADIENT_SEGMENT_WIDTH{ 1.0f };

    typedef ImOk::LinearSrgb (*GradientColorFn)(float t);

    // The worst difference, in 8-bit steps, between a gradient drawn `length` px long and its
    // exact colors at every pixel center. The GPU interpolates each channel along a segment.
    float WorstGradientDiff(GradientColorFn colorAt, int length)
    {
        const int segments{ static_cast<int>(std::ceil(length / GRADIENT_SEGMENT_WIDTH)) };
        std::vector<Rgb8> vertices(static_cast<size_t>(segments + 1)); // Parentheses: size, not a list
        for (int i{ 0 }; i <= segments; ++i)
        {
            vertices[static_cast<size_t>(i)] = ToRgb8(colorAt(static_cast<float>(i) / segments));
        }

        float worstDiff{ 0.0f };
        for (int p{ 0 }; p < length; ++p)
        {
            const float t{ (p + 0.5f) / length };
            const float f{ t * segments };
            const int i{ std::min(static_cast<int>(f), segments - 1) };
            const float u{ f - i };
            const Rgb8& from{ vertices[static_cast<size_t>(i)] };
            const Rgb8& to{ vertices[static_cast<size_t>(i + 1)] };

            // The GPU writes the interpolated value to an 8-bit target, rounding to nearest
            const Rgb8 drawn{ std::floor(from.r + (to.r - from.r) * u + 0.5f),
                              std::floor(from.g + (to.g - from.g) * u + 0.5f),
                              std::floor(from.b + (to.b - from.b) * u + 0.5f) };
            const Rgb8 exact{ ToRgb8(colorAt(t)) };

            worstDiff = std::fmax(worstDiff, std::fmax(std::fabs(drawn.r - exact.r),
                                             std::fmax(std::fabs(drawn.g - exact.g), std::fabs(drawn.b - exact.b))));
        }
        return worstDiff;
    }

    // The bars as imok.cpp draws them. Measured along t, so vertical bars (t flipped) match.
    ImOk::LinearSrgb HueBarAt(float t)
    {
        return ImOk::Internal::HueBarColor(t * 360.0f);
    }

    ImOk::LinearSrgb TemperatureBarAt(float t)
    {
        return ImOk::KelvinToLinearSrgb(ImOk::Internal::BarPositionToKelvin(t));
    }

    // Gradients whose whole path stays inside sRGB, so the only error is the mesh's
    ImOk::LinearSrgb LinearBlackToWhiteAt(float t)
    {
        return ImOk::LerpLinearSrgb({ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, t);
    }

    ImOk::LinearSrgb OkLabBlackToWhiteAt(float t)
    {
        return ImOk::OkLabToLinearSrgb(ImOk::LerpOkLab({ 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, t));
    }

    // A full hue circle at the largest chroma inside sRGB at that lightness (the circle of
    // Ottosson's OkLCh picker)
    ImOk::LinearSrgb OkLChHueCircleAt(float t)
    {
        const ImOk::OkLCh from{ 0.7502f, 0.127552f, 0.0f };
        const ImOk::OkLCh to{ 0.7502f, 0.127552f, 359.99f };
        return ImOk::OkLabToLinearSrgb(ImOk::OkLChToOkLab(ImOk::LerpOkLCh(from, to, t, ImOk::HueDirection::Increasing)));
    }

    // Pure blue to yellow in Oklab, as the README's gradients.png: the path leaves sRGB right
    // after blue, and each vertex is clipped
    ImOk::LinearSrgb OkLabBlueToYellowAt(float t)
    {
        const ImOk::OkLab blue{ ImOk::LinearSrgbToOkLab({ 0.0f, 0.0f, 1.0f }) };
        const ImOk::OkLab yellow{ ImOk::LinearSrgbToOkLab({ 1.0f, 1.0f, 0.0f }) };
        return ImOk::OkLabToLinearSrgb(ImOk::LerpOkLab(blue, yellow, t));
    }

    // Bar and gradient lengths as drawn: the popup's hue bar at the default style (205 px, 182
    // with an alpha bar), the vertical light bars' default (129), a narrow window (120)
    constexpr int LONG_LENGTHS[]{ 205, 182, 129, 120 };
    // A short bar (64): a steep curve covers fewer pixels, so the error grows
    constexpr int SHORT_LENGTH{ 64 };

    struct GradientCase
    {
        const char* name{ nullptr };
        GradientColorFn colorAt{ nullptr };
        float longBound{ 0.0f };  // At LONG_LENGTHS
        float shortBound{ 0.0f }; // At SHORT_LENGTH
    };

    void CheckGradient(const GradientCase& gradient)
    {
        INFO(gradient.name);
        for (const int length : LONG_LENGTHS)
        {
            INFO("length ", length);
            CHECK(WorstGradientDiff(gradient.colorAt, length) <= gradient.longBound);
        }
        INFO("length ", SHORT_LENGTH);
        CHECK(WorstGradientDiff(gradient.colorAt, SHORT_LENGTH) <= gradient.shortBound);
    }
}

TEST_CASE("Bar meshes stay within 2 8-bit steps of the exact colors")
{
    // The error is where the encoded colors curve most: the hue bar near 142 and 264 degrees,
    // the temperature bar at 1901 K, where blue leaves 0.
    // The intensity bar is a gray ramp even in Oklab L, as OkLab black to white below.
    CheckGradient({ "hue bar", HueBarAt, 2.0f, 3.0f });
    CheckGradient({ "temperature bar", TemperatureBarAt, 2.0f, 2.0f });
}

TEST_CASE("Gradient meshes inside sRGB follow the exact colors")
{
    // Linear light is the least even once encoded: the sRGB curve is steepest near black
    CheckGradient({ "LinearSrgb black to white", LinearBlackToWhiteAt, 2.0f, 5.0f });
    CheckGradient({ "OkLab black to white", OkLabBlackToWhiteAt, 1.0f, 1.0f });
    CheckGradient({ "OkLCh hue circle", OkLChHueCircleAt, 1.0f, 2.0f });
}

TEST_CASE("Gradients leaving sRGB: reported, not bounded")
{
    // Each vertex is clipped, so the drawn mesh cuts straight across the corners of the clipped
    // path; a finer mesh barely helps. Here the jump sits in the first pixel, from pure blue
    // (in gamut, unchanged) to the clipped colors right after it.
    MESSAGE("OkLab blue to yellow at 205 px: ", WorstGradientDiff(OkLabBlueToYellowAt, 205), " 8-bit steps");
}