// ImOk color math internals
// See LICENSE.txt for copyright and licensing details (standard MIT License).
// Gamut geometry ported from Ottosson's ok_color.h (MIT: the notice is in imok_color.cpp).
// Not part of the stable API: may change without notice.
// Hue directions (a, b) are unit vectors: a * a + b * b == 1.

#pragma once

#include "imok_color.h"

namespace ImOk
{
    namespace Internal
    {
        // Below this chroma a color is gray, with no hue. Far below visibility (a JND is ~0.02).
        constexpr float ACHROMATIC_CHROMA{ 1e-5f };

        constexpr float IMOK_PI{ 3.14159265358979f };
        constexpr float DEG_TO_RAD{ IMOK_PI / 180.0f };
        constexpr float RAD_TO_DEG{ 180.0f / IMOK_PI };

        // Oklab lightness and chroma
        struct LC
        {
            float L{ 0.0f };
            float C{ 0.0f };
        };

        // The cusp as S = C / L and T = C / (1 - L). At lightness L, the triangle approximation's
        // max chroma is min(S * L, T * (1 - L)).
        struct ST
        {
            float S{ 0.0f };
            float T{ 0.0f };
        };

        // Okhsl's chroma at one lightness and hue: C_0 near s = 0, C_mid at s = 0.8, C_max (the
        // sRGB gamut edge) at s = 1.
        struct Cs
        {
            float C_0{ 0.0f };
            float C_mid{ 0.0f };
            float C_max{ 0.0f };
        };

        // Max saturation S = C / L inside the sRGB gamut, for a hue direction
        float ComputeMaxSaturation(float a, float b);

        // The most chromatic color of a hue inside the sRGB gamut, as (L, C).
        //
        // At blue the gamut folds in Oklab: just above blue's hue, the hue's ray leaves the gamut
        // through red, comes back in, and leaves through green, so there are two cusps. FindCusp
        // returns the outer one. The cusp therefore jumps at blue's hue, which is the gamut, not an
        // error; just above it, Okhsv and Okhsl land a hair outside the gamut, and display clamps
        // that. Exactly at blue's hue, rounding alone would pick between the two cusps; ImOk takes
        // the outer one (a DEVIATION from Ottosson, see ComputeMaxSaturation), so pure blue
        // round-trips through Okhsv and Okhsl on every compiler.
        LC FindCusp(float a, float b);

        // Where a line in one hue's (L, C) plane meets the sRGB gamut boundary. The line runs from
        // (L0, 0) on the gray axis, L0 in [0, 1], to (L1, C1):
        //     L = L0 * (1 - t) + t * L1
        //     C = t * C1
        // Returns t at the boundary. Pass the cusp when you already have it.
        float FindGamutIntersection(float a, float b, float L1, float C1, float L0, const LC& cusp);
        float FindGamutIntersection(float a, float b, float L1, float C1, float L0);

        ST ToST(const LC& cusp);

        // A smooth approximation of the cusp as ST, with S_mid < S_max and T_mid < T_max
        ST GetSTMid(float a_, float b_);

        // Okhsl's chroma values for L in (0, 1) and a hue
        Cs GetCs(float L, float a_, float b_);

        // Scaled so the largest channel is 1: same chromaticity, no brightness. The largest
        // channel must be above 0.
        LinearSrgb ScaleToLargestChannel(const LinearSrgb& linear);

        // Clamped to [0, 1], rounded to nearest
        int ChannelToByte(float x);

        enum class HexForms
        {
            All,      // #RGB, #RGBA, #RRGGBB, #RRGGBBAA
            LongOnly, // #RRGGBB, #RRGGBBAA: for fields that apply each keystroke, where "#ABC" would apply on the way to "#ABCDEF"
        };

        bool HexToEncodedSrgb(const char* text, EncodedSrgb* encoded, float* alpha, HexForms forms);
    }
}