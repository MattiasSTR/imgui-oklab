#pragma once

#include <cmath>
#include "imok_color.h"

namespace ImOkTest
{
    // Smallest angle between two hues, in degrees
    inline float HueDistance(float a, float b)
    {
        const float d{ std::fabs(a - b) };
        return std::fmin(d, 360.0f - d);
    }

    // 0 on the sRGB gamut boundary; positive outside it, or inside without touching it
    inline float GamutBoundaryError(const ImOk::LinearSrgb& rgb)
    {
        const float maxChannel{ std::fmax(std::fmax(rgb.r, rgb.g), rgb.b) };
        const float minChannel{ std::fmin(std::fmin(rgb.r, rgb.g), rgb.b) };

        const float outside{ std::fmax(std::fmax(maxChannel - 1.0f, -minChannel), 0.0f) };
        const float touching{ std::fmin(std::fabs(maxChannel - 1.0f), std::fabs(minChannel)) };
        return std::fmax(outside, touching);
    }
}