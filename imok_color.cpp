// ImOk color math: conversions
// ImOk's code: see LICENSE.txt for copyright and licensing details (standard MIT License).
//
// Parts of this file are derived from Björn Ottosson's ok_color.h
// (https://bottosson.github.io/posts/colorpicker/), used under the MIT license:
//
// Copyright (c) 2021 Björn Ottosson
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// UTF-8 with BOM, for the name above. Keep the BOM, or MSVC reads this file in the system code page.

#include "imok_color.h"
#include "imok_color_internal.h"
#include <cmath>
#include <cfloat>

namespace ImOk
{
    namespace
    {
        using Internal::DEG_TO_RAD;
        using Internal::RAD_TO_DEG;

        // Ottosson's toe constants
        constexpr float TOE_K1{ 0.206f };
        constexpr float TOE_K2{ 0.03f };
        constexpr float TOE_K3{ (1.0f + TOE_K1) / (1.0f + TOE_K2) };

        // Ottosson's S_0
        constexpr float OKHSV_S0{ 0.5f };

        // Okhsl: the s where chroma reaches C_mid, and its inverse
        constexpr float OKHSL_MID{ 0.8f };
        constexpr float OKHSL_MID_INV{ 1.25f };

        using Internal::ACHROMATIC_CHROMA;

        // ComputeMaxSaturation's coefficients, one set per channel
        struct MaxSaturationCoefficients
        {
            float k0, k1, k2, k3, k4; // Polynomial approximation of max saturation
            float wl, wm, ws;         // Row of the LMS -> linear sRGB matrix for that channel
        };

        constexpr MaxSaturationCoefficients MAX_SATURATION_RED{
            +1.19086277f, +1.76576728f, +0.59662641f, +0.75515197f, +0.56771245f,
            +4.0767416621f, -3.3077115913f, +0.2309699292f };

        constexpr MaxSaturationCoefficients MAX_SATURATION_GREEN{
            +0.73956515f, -0.45954404f, +0.08285427f, +0.12541070f, +0.14503204f,
            -1.2684380046f, +2.6097574011f, -0.3413193965f };

        constexpr MaxSaturationCoefficients MAX_SATURATION_BLUE{
            +1.35733652f, -0.00915799f, -1.15130210f, -0.50559606f, +0.00692167f,
            -0.0041960863f, -0.7034186147f, +1.7076147010f };

        // Both region tests within this of 1 is blue's corner. Pure blues land within ~1.4e-6 on
        // every build tested; 1e-5 is ~3e-4 degrees of hue on each side.
        constexpr float BLUE_CORNER_TOLERANCE{ 1e-5f };

        // Sign-preserving: out-of-gamut and HDR values survive
        float EncodeSrgbChannel(float x)
        {
            const float a{ std::fabs(x) };
            // 1.055f * 1 - 0.055f rounds to 0.99999994: without this, white encodes 1 ulp below 1
            if (a == 1.0f)
            {
                return std::copysign(1.0f, x);
            }
            const float encoded{ (a <= 0.0031308f)
                ? 12.92f * a
                : 1.055f * std::pow(a, 1.0f / 2.4f) - 0.055f };
            return std::copysign(encoded, x);
        }

        float DecodeSrgbChannel(float x)
        {
            const float a{ std::fabs(x) };
            const float decoded{ (a <= 0.04045f)
                ? a / 12.92f
                : std::pow((a + 0.055f) / 1.055f, 2.4f) };
            return std::copysign(decoded, x);
        }

        float Clamp01(float x)
        {
            return std::fmin(std::fmax(x, 0.0f), 1.0f);
        }

        // -1, 0 or 1 (Ottosson's sgn)
        float Sign(float x)
        {
            return static_cast<float>(0.0f < x) - static_cast<float>(x < 0.0f);
        }

        // Exact at both ends: t = 0 gives a, t = 1 gives b
        float Lerp(float a, float b, float t)
        {
            return (1.0f - t) * a + t * b;
        }

        float WrapHue(float h)
        {
            float wrapped{ std::fmod(h, 360.0f) };
            if (wrapped < 0.0f)
            {
                wrapped += 360.0f;
            }
            if (wrapped >= 360.0f) // A tiny negative value + 360 can round to exactly 360.0f
            {
                wrapped -= 360.0f;
            }
            return wrapped;
        }

        int HexDigitValue(char c)
        {
            if (c >= '0' && c <= '9') { return c - '0'; }
            if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
            if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
            return -1;
        }

        bool IsBlank(char c)
        {
            return c == ' ' || c == '\t';
        }
    }

    LinearSrgb EncodedSrgbToLinearSrgb(const EncodedSrgb& encoded)
    {
        return { DecodeSrgbChannel(encoded.r), DecodeSrgbChannel(encoded.g), DecodeSrgbChannel(encoded.b) };
    }

    EncodedSrgb LinearSrgbToEncodedSrgb(const LinearSrgb& linear)
    {
        return { EncodeSrgbChannel(linear.r), EncodeSrgbChannel(linear.g), EncodeSrgbChannel(linear.b) };
    }

    OkLab LinearSrgbToOkLab(const LinearSrgb& linear)
    {
        // Linear sRGB -> approximate cone responses (LMS)
        const float l{ 0.4122214708f * linear.r + 0.5363325363f * linear.g + 0.0514459929f * linear.b };
        const float m{ 0.2119034982f * linear.r + 0.6806995451f * linear.g + 0.1073969566f * linear.b };
        const float s{ 0.0883024619f * linear.r + 0.2817188376f * linear.g + 0.6299787005f * linear.b };

        // std::cbrt, not pow: negative values stay valid
        const float l_{ std::cbrt(l) };
        const float m_{ std::cbrt(m) };
        const float s_{ std::cbrt(s) };

        return {
            0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_,
            1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_,
            0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_,
        };
    }

    LinearSrgb OkLabToLinearSrgb(const OkLab& lab)
    {
        const float l_{ lab.L + 0.3963377774f * lab.a + 0.2158037573f * lab.b };
        const float m_{ lab.L - 0.1055613458f * lab.a - 0.0638541728f * lab.b };
        const float s_{ lab.L - 0.0894841775f * lab.a - 1.2914855480f * lab.b };

        const float l{ l_ * l_ * l_ };
        const float m{ m_ * m_ * m_ };
        const float s{ s_ * s_ * s_ };

        return {
            +4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
            -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
            -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s,
        };
    }

    OkLCh OkLabToOkLCh(const OkLab& lab)
    {
        const float C{ std::sqrt(lab.a * lab.a + lab.b * lab.b) };

        float h{ 0.0f };
        if (C >= ACHROMATIC_CHROMA)
        {
            h = std::atan2(lab.b, lab.a) * RAD_TO_DEG;
            if (h < 0.0f)
            {
                h += 360.0f;
            }
            if (h >= 360.0f) // A tiny negative angle + 360 can round to exactly 360.0f
            {
                h -= 360.0f;
            }
        }

        return { lab.L, C, h };
    }

    OkLab OkLChToOkLab(const OkLCh& lch)
    {
        const float hRad{ lch.h * DEG_TO_RAD };
        return { lch.L, lch.C * std::cos(hRad), lch.C * std::sin(hRad) };
    }

    float Toe(float L)
    {
        const float t{ TOE_K3 * L - TOE_K1 };
        return 0.5f * (t + std::sqrt(t * t + 4.0f * TOE_K2 * TOE_K3 * L));
    }

    float ToeInv(float Lr)
    {
        return (Lr * Lr + TOE_K1 * Lr) / (TOE_K3 * (Lr + TOE_K2));
    }

    Okhsv OkLabToOkhsv(const OkLab& lab)
    {
        // Black: the math below would divide by zero
        if (lab.L <= 0.0f)
        {
            return { 0.0f, 0.0f, 0.0f };
        }

        const OkLCh lch{ OkLabToOkLCh(lab) };
        const float C{ lch.C };

        // DEVIATION from Ottosson: he divides by C, NaN for grays. Any unit vector gives the same
        // s and v there.
        const bool isGray{ C < ACHROMATIC_CHROMA };
        const float a_{ isGray ? 1.0f : lab.a / C };
        const float b_{ isGray ? 0.0f : lab.b / C };

        const Internal::LC cusp{ Internal::FindCusp(a_, b_) };
        const Internal::ST ST_max{ Internal::ToST(cusp) };
        const float S_max{ ST_max.S };
        const float T_max{ ST_max.T };
        const float k{ 1.0f - OKHSV_S0 / S_max };

        // First find L_v, C_v, L_vt and C_vt
        const float t{ T_max / (C + lab.L * T_max) };
        const float L_v{ t * lab.L };
        const float C_v{ t * C };

        const float L_vt{ ToeInv(L_v) };
        const float C_vt{ C_v * L_vt / L_v };

        // Invert the step that compensates for the toe and the curved top of the triangle
        const LinearSrgb rgb_scale{ OkLabToLinearSrgb({ L_vt, a_ * C_vt, b_ * C_vt }) };
        const float scale_L{ std::cbrt(1.0f / std::fmax(std::fmax(rgb_scale.r, rgb_scale.g), std::fmax(rgb_scale.b, 0.0f))) };

        const float L{ Toe(lab.L / scale_L) };

        // Now compute v and s
        const float v{ L / L_v };
        const float s{ (OKHSV_S0 + T_max) * C_v / ((T_max * OKHSV_S0) + T_max * k * C_v) };

        return { lch.h, s, v };
    }

    OkLab OkhsvToOkLab(const Okhsv& hsv)
    {
        // Black for any h and s: the math below would divide by zero
        if (hsv.v <= 0.0f)
        {
            return { 0.0f, 0.0f, 0.0f };
        }

        const float hRad{ hsv.h * DEG_TO_RAD };
        const float a_{ std::cos(hRad) };
        const float b_{ std::sin(hRad) };

        const Internal::LC cusp{ Internal::FindCusp(a_, b_) };
        const Internal::ST ST_max{ Internal::ToST(cusp) };
        const float S_max{ ST_max.S };
        const float T_max{ ST_max.T };
        const float k{ 1.0f - OKHSV_S0 / S_max };

        // First compute L and C as if the gamut is a perfect triangle. L, C when v == 1:
        const float denominator{ OKHSV_S0 + T_max - T_max * k * hsv.s };
        const float L_v{ 1.0f - hsv.s * OKHSV_S0 / denominator };
        const float C_v{ hsv.s * T_max * OKHSV_S0 / denominator };

        float L{ hsv.v * L_v };
        float C{ hsv.v * C_v };

        // Then compensate for both the toe and the curved top part of the triangle
        const float L_vt{ ToeInv(L_v) };
        const float C_vt{ C_v * L_vt / L_v };

        const float L_new{ ToeInv(L) };
        C = C * L_new / L;
        L = L_new;

        const LinearSrgb rgb_scale{ OkLabToLinearSrgb({ L_vt, a_ * C_vt, b_ * C_vt }) };
        const float scale_L{ std::cbrt(1.0f / std::fmax(std::fmax(rgb_scale.r, rgb_scale.g), std::fmax(rgb_scale.b, 0.0f))) };

        L = L * scale_L;
        C = C * scale_L;

        return { L, C * a_, C * b_ };
    }

    Okhsl OkLabToOkhsl(const OkLab& lab)
    {
        // Black: the math below would divide by zero
        if (lab.L <= 0.0f)
        {
            return { 0.0f, 0.0f, 0.0f };
        }

        const OkLCh lch{ OkLabToOkLCh(lab) };
        const float C{ lch.C };
        const float L{ lab.L };

        // DEVIATION from Ottosson: he divides by C, NaN for grays and unstable near white. s is 0
        // in the limit.
        if (C < ACHROMATIC_CHROMA)
        {
            return { 0.0f, 0.0f, Toe(L) };
        }

        const float a_{ lab.a / C };
        const float b_{ lab.b / C };

        const Internal::Cs cs{ Internal::GetCs(L, a_, b_) };
        const float C_0{ cs.C_0 };
        const float C_mid{ cs.C_mid };
        const float C_max{ cs.C_max };

        // Inverse of the interpolation in OkhslToOkLab
        float s{ 0.0f };
        if (C < C_mid)
        {
            const float k_1{ OKHSL_MID * C_0 };
            const float k_2{ 1.0f - k_1 / C_mid };

            const float t{ C / (k_1 + k_2 * C) };
            s = t * OKHSL_MID;
        }
        else
        {
            const float k_0{ C_mid };
            const float k_1{ (1.0f - OKHSL_MID) * C_mid * C_mid * OKHSL_MID_INV * OKHSL_MID_INV / C_0 };
            const float k_2{ 1.0f - k_1 / (C_max - C_mid) };

            const float t{ (C - k_0) / (k_1 + k_2 * (C - k_0)) };
            s = OKHSL_MID + (1.0f - OKHSL_MID) * t;
        }

        return { lch.h, s, Toe(L) };
    }

    OkLab OkhslToOkLab(const Okhsl& hsl)
    {
        // DEVIATION from Ottosson: he checks l == 1 and l == 0 only; beyond them his math breaks
        // down, so those snap too
        if (hsl.l >= 1.0f)
        {
            return { 1.0f, 0.0f, 0.0f };
        }
        if (hsl.l <= 0.0f)
        {
            return { 0.0f, 0.0f, 0.0f };
        }

        const float hRad{ hsl.h * DEG_TO_RAD };
        const float a_{ std::cos(hRad) };
        const float b_{ std::sin(hRad) };
        const float L{ ToeInv(hsl.l) };

        const Internal::Cs cs{ Internal::GetCs(L, a_, b_) };
        const float C_0{ cs.C_0 };
        const float C_mid{ cs.C_mid };
        const float C_max{ cs.C_max };

        // Interpolate the three chroma values so that:
        //   at s = 0:   dC/ds = C_0, C = 0
        //   at s = 0.8: C = C_mid
        //   at s = 1.0: C = C_max
        float C{ 0.0f };
        if (hsl.s < OKHSL_MID)
        {
            const float t{ OKHSL_MID_INV * hsl.s };

            const float k_1{ OKHSL_MID * C_0 };
            const float k_2{ 1.0f - k_1 / C_mid };

            C = t * k_1 / (1.0f - k_2 * t);
        }
        else
        {
            const float t{ (hsl.s - OKHSL_MID) / (1.0f - OKHSL_MID) };

            const float k_0{ C_mid };
            const float k_1{ (1.0f - OKHSL_MID) * C_mid * C_mid * OKHSL_MID_INV * OKHSL_MID_INV / C_0 };
            const float k_2{ 1.0f - k_1 / (C_max - C_mid) };

            C = k_0 + t * k_1 / (1.0f - k_2 * t);
        }

        return { L, C * a_, C * b_ };
    }

    bool IsInSrgbGamut(const LinearSrgb& linear)
    {
        return linear.r >= 0.0f && linear.r <= 1.0f
            && linear.g >= 0.0f && linear.g <= 1.0f
            && linear.b >= 0.0f && linear.b <= 1.0f;
    }

    LinearSrgb ClipToSrgbGamut(const LinearSrgb& linear, GamutClipMethod method, float adaptiveAlpha)
    {
        // DEVIATION from Ottosson: he skips only colors strictly inside, so the boundary
        // (primaries, white, black) moved by up to ~5e-4
        if (IsInSrgbGamut(linear))
        {
            return linear;
        }

        const OkLab lab{ LinearSrgbToOkLab(linear) };
        const float L{ lab.L };
        const float C{ std::sqrt(lab.a * lab.a + lab.b * lab.b) };

        // DEVIATION from Ottosson: his C = max(eps, C) leaves (a_, b_) far from a unit vector for
        // grays. With no hue, every method reduces to clamping each channel.
        if (C < ACHROMATIC_CHROMA)
        {
            return { Clamp01(linear.r), Clamp01(linear.g), Clamp01(linear.b) };
        }

        const float a_{ lab.a / C };
        const float b_{ lab.b / C };

        // Once, shared with FindGamutIntersection
        const Internal::LC cusp{ Internal::FindCusp(a_, b_) };

        // The point on the gray axis to project toward
        float L0{ 0.5f };
        switch (method)
        {
        case GamutClipMethod::PreserveLightness:
            L0 = Clamp01(L);
            break;

        case GamutClipMethod::ProjectToMidGray:
            L0 = 0.5f;
            break;

        case GamutClipMethod::ProjectToCuspLightness:
            L0 = cusp.L;
            break;

        case GamutClipMethod::AdaptiveMidGray:
        {
            const float Ld{ L - 0.5f };
            const float e1{ 0.5f + std::fabs(Ld) + adaptiveAlpha * C };
            L0 = 0.5f * (1.0f + Sign(Ld) * (e1 - std::sqrt(e1 * e1 - 2.0f * std::fabs(Ld))));
            break;
        }

        case GamutClipMethod::AdaptiveCuspLightness:
        {
            const float Ld{ L - cusp.L };
            const float k{ 2.0f * (Ld > 0.0f ? 1.0f - cusp.L : cusp.L) };
            const float e1{ 0.5f * k + std::fabs(Ld) + adaptiveAlpha * C / k };
            L0 = cusp.L + 0.5f * (Sign(Ld) * (e1 - std::sqrt(e1 * e1 - 2.0f * k * std::fabs(Ld))));
            break;
        }
        }

        const float t{ Internal::FindGamutIntersection(a_, b_, L, C, L0, cusp) };
        const float L_clipped{ L0 * (1.0f - t) + t * L };
        const float C_clipped{ t * C };

        return OkLabToLinearSrgb({ L_clipped, C_clipped * a_, C_clipped * b_ });
    }

    float RelativeLuminance(const LinearSrgb& linear)
    {
        // WCAG's coefficients, rounded to sum to 1
        return 0.2126f * linear.r + 0.7152f * linear.g + 0.0722f * linear.b;
    }

    LinearSrgb KelvinToLinearSrgb(float kelvin)
    {
        const float T{ std::fmin(std::fmax(kelvin, MIN_KELVIN), MAX_KELVIN) };
        const float T2{ T * T };

        // Krystek (1985): the Planckian locus in CIE 1960 (u, v), rational in T
        const float u{ (0.860117757f + 1.54118254e-4f * T + 1.28641212e-7f * T2)
                     / (1.0f + 8.42420235e-4f * T + 7.08145163e-7f * T2) };
        const float v{ (0.317398726f + 4.22806245e-5f * T + 4.20481691e-8f * T2)
                     / (1.0f - 2.89741816e-5f * T + 1.61456053e-7f * T2) };

        // (u, v) to (x, y), then XYZ with Y = 1
        const float d{ 2.0f * u - 8.0f * v + 4.0f };
        const float x{ 3.0f * u / d };
        const float y{ 2.0f * v / d };
        const float X{ x / y };
        const float Z{ (1.0f - x - y) / y };

        // No chromatic adaptation: the light's own color, not white-balanced
        const LinearSrgb linear{
            +3.2404542f * X - 1.5371385f - 0.4985314f * Z,
            -0.9692660f * X + 1.8760108f + 0.0415560f * Z,
            +0.0556434f * X - 0.2040259f + 1.0572252f * Z };

        // Below ~1900 K blue goes negative. Scale first so only chroma is out, then clip keeping hue.
        const LinearSrgb scaled{ Internal::ScaleToLargestChannel(linear) };
        if (IsInSrgbGamut(scaled))
        {
            return scaled;
        }
        const LinearSrgb clipped{ ClipToSrgbGamut(scaled, GamutClipMethod::PreserveLightness, 0.0f) };
        const LinearSrgb rescaled{ Internal::ScaleToLargestChannel(clipped) };

        // The clip can land a hair outside [0, 1]
        return { Clamp01(rescaled.r), Clamp01(rescaled.g), Clamp01(rescaled.b) };
    }

    bool HexToEncodedSrgb(const char* text, EncodedSrgb* encoded, float* alpha)
    {
        return Internal::HexToEncodedSrgb(text, encoded, alpha, Internal::HexForms::All);
    }

    void EncodedSrgbToHex(const EncodedSrgb& encoded, char out[HEX_BUFFER_SIZE], const float* alpha)
    {
        constexpr char DIGITS[]{ "0123456789ABCDEF" };
        const int bytes[4]{ Internal::ChannelToByte(encoded.r), Internal::ChannelToByte(encoded.g),
                            Internal::ChannelToByte(encoded.b),
                            (alpha != nullptr) ? Internal::ChannelToByte(*alpha) : 0 };
        const int count{ (alpha != nullptr) ? 4 : 3 };

        int length{ 0 };
        out[length++] = '#';
        for (int i{ 0 }; i < count; ++i)
        {
            out[length++] = DIGITS[bytes[i] >> 4];
            out[length++] = DIGITS[bytes[i] & 0xF];
        }
        out[length] = '\0';
    }

    LinearSrgb LerpLinearSrgb(const LinearSrgb& from, const LinearSrgb& to, float t)
    {
        return { Lerp(from.r, to.r, t), Lerp(from.g, to.g, t), Lerp(from.b, to.b, t) };
    }

    OkLab LerpOkLab(const OkLab& from, const OkLab& to, float t)
    {
        return { Lerp(from.L, to.L, t), Lerp(from.a, to.a, t), Lerp(from.b, to.b, t) };
    }

    OkLCh LerpOkLCh(const OkLCh& from, const OkLCh& to, float t, HueDirection direction)
    {
        float hueFrom{ WrapHue(from.h) };
        float hueTo{ WrapHue(to.h) };

        // A gray has no meaningful hue: borrow the other endpoint's (CSS "missing hue")
        const bool isGrayFrom{ from.C < ACHROMATIC_CHROMA };
        const bool isGrayTo{ to.C < ACHROMATIC_CHROMA };
        if (isGrayFrom && !isGrayTo)
        {
            hueFrom = hueTo;
        }
        else if (isGrayTo && !isGrayFrom)
        {
            hueTo = hueFrom;
        }

        // Adjust the hue difference for the requested direction (CSS Color 4 rules)
        float delta{ hueTo - hueFrom }; // In (-360, 360)
        switch (direction)
        {
        case HueDirection::Shorter:
            if (delta > 180.0f)
            {
                delta -= 360.0f;
            }
            else if (delta < -180.0f)
            {
                delta += 360.0f;
            }
            break;

        case HueDirection::Longer:
            if (delta > 0.0f && delta < 180.0f)
            {
                delta -= 360.0f;
            }
            else if (delta > -180.0f && delta <= 0.0f)
            {
                delta += 360.0f;
            }
            break;

        case HueDirection::Increasing:
            if (delta < 0.0f)
            {
                delta += 360.0f;
            }
            break;

        case HueDirection::Decreasing:
            if (delta > 0.0f)
            {
                delta -= 360.0f;
            }
            break;
        }

        return { Lerp(from.L, to.L, t), Lerp(from.C, to.C, t), WrapHue(hueFrom + delta * t) };
    }

    namespace Internal
    {
        float ComputeMaxSaturation(float a, float b)
        {
            // Max saturation is reached when one of r, g or b drops below zero.
            // Which channel that is depends on the hue, so pick coefficients per region.
            const float redTest{ -1.88170328f * a - 0.80936493f * b };
            const float greenTest{ 1.81444104f * a - 1.19445276f * b };

            // DEVIATION from Ottosson: both region tests pass through blue's hue, where red and green
            // reach 0 together, so there both are 1 up to rounding. Rounding then picked the branch
            // (a CRT's cbrtf, FMA): red, or the yellows' branch far outside the gamut. Take green,
            // whose solution is pure blue (see FindCusp).
            const bool isBlueCorner{ std::fabs(redTest - 1.0f) < BLUE_CORNER_TOLERANCE
                                     && std::fabs(greenTest - 1.0f) < BLUE_CORNER_TOLERANCE };
            const MaxSaturationCoefficients& k{
                isBlueCorner       ? MAX_SATURATION_GREEN :
                (redTest > 1.0f)   ? MAX_SATURATION_RED :
                (greenTest > 1.0f) ? MAX_SATURATION_GREEN :
                                     MAX_SATURATION_BLUE };

            // Polynomial approximation
            float S{ k.k0 + k.k1 * a + k.k2 * b + k.k3 * a * a + k.k4 * a * b };

            // One step of Halley's method: error below ~1e-6, except near blue's hue
            const float k_l{ +0.3963377774f * a + 0.2158037573f * b };
            const float k_m{ -0.1055613458f * a - 0.0638541728f * b };
            const float k_s{ -0.0894841775f * a - 1.2914855480f * b };
            {
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

                const float f { k.wl * l     + k.wm * m     + k.ws * s };
                const float f1{ k.wl * l_dS  + k.wm * m_dS  + k.ws * s_dS };
                const float f2{ k.wl * l_dS2 + k.wm * m_dS2 + k.ws * s_dS2 };

                S = S - f * f1 / (f1 * f1 - 0.5f * f * f2);
            }

            return S;
        }

        LC FindCusp(float a, float b)
        {
            // Maximum saturation (S = C / L) for this hue
            const float sCusp{ ComputeMaxSaturation(a, b) };

            // Scale lightness until the brightest channel reaches exactly 1
            const LinearSrgb rgbAtMax{ OkLabToLinearSrgb({ 1.0f, sCusp * a, sCusp * b }) };
            const float lCusp{ std::cbrt(1.0f / std::fmax(std::fmax(rgbAtMax.r, rgbAtMax.g), rgbAtMax.b)) };

            return { lCusp, lCusp * sCusp };
        }

        float FindGamutIntersection(float a, float b, float L1, float C1, float L0, const LC& cusp)
        {
            // Find the intersection for the upper and lower half separately
            float t{ 0.0f };
            if (((L1 - L0) * cusp.C - (cusp.L - L0) * C1) <= 0.0f)
            {
                // Lower half: the triangle edge is exact here
                t = cusp.C * L0 / (C1 * cusp.L + cusp.C * (L0 - L1));
            }
            else
            {
                // Upper half: first intersect with the triangle
                t = cusp.C * (L0 - 1.0f) / (C1 * (cusp.L - 1.0f) + cusp.C * (L0 - L1));

                // Then one step of Halley's method
                {
                    const float dL{ L1 - L0 };
                    const float dC{ C1 };

                    const float k_l{ +0.3963377774f * a + 0.2158037573f * b };
                    const float k_m{ -0.1055613458f * a - 0.0638541728f * b };
                    const float k_s{ -0.0894841775f * a - 1.2914855480f * b };

                    const float l_dt{ dL + dC * k_l };
                    const float m_dt{ dL + dC * k_m };
                    const float s_dt{ dL + dC * k_s };

                    // Per Ottosson: 2 or 3 iterations of this block if higher accuracy is required
                    {
                        const float L{ L0 * (1.0f - t) + t * L1 };
                        const float C{ t * C1 };

                        const float l_{ L + C * k_l };
                        const float m_{ L + C * k_m };
                        const float s_{ L + C * k_s };

                        const float l{ l_ * l_ * l_ };
                        const float m{ m_ * m_ * m_ };
                        const float s{ s_ * s_ * s_ };

                        const float ldt{ 3.0f * l_dt * l_ * l_ };
                        const float mdt{ 3.0f * m_dt * m_ * m_ };
                        const float sdt{ 3.0f * s_dt * s_ * s_ };

                        const float ldt2{ 6.0f * l_dt * l_dt * l_ };
                        const float mdt2{ 6.0f * m_dt * m_dt * m_ };
                        const float sdt2{ 6.0f * s_dt * s_dt * s_ };

                        const float r { 4.0767416621f * l    - 3.3077115913f * m    + 0.2309699292f * s - 1.0f };
                        const float r1{ 4.0767416621f * ldt  - 3.3077115913f * mdt  + 0.2309699292f * sdt };
                        const float r2{ 4.0767416621f * ldt2 - 3.3077115913f * mdt2 + 0.2309699292f * sdt2 };

                        const float u_r{ r1 / (r1 * r1 - 0.5f * r * r2) };
                        float t_r{ -r * u_r };

                        const float g { -1.2684380046f * l    + 2.6097574011f * m    - 0.3413193965f * s - 1.0f };
                        const float g1{ -1.2684380046f * ldt  + 2.6097574011f * mdt  - 0.3413193965f * sdt };
                        const float g2{ -1.2684380046f * ldt2 + 2.6097574011f * mdt2 - 0.3413193965f * sdt2 };

                        const float u_g{ g1 / (g1 * g1 - 0.5f * g * g2) };
                        float t_g{ -g * u_g };

                        // Ottosson's 'b', renamed: it shadows the parameter b (C4457)
                        const float bl{ -0.0041960863f * l    - 0.7034186147f * m    + 1.7076147010f * s - 1.0f };
                        const float b1{ -0.0041960863f * ldt  - 0.7034186147f * mdt  + 1.7076147010f * sdt };
                        const float b2{ -0.0041960863f * ldt2 - 0.7034186147f * mdt2 + 1.7076147010f * sdt2 };

                        const float u_b{ b1 / (b1 * b1 - 0.5f * bl * b2) };
                        float t_b{ -bl * u_b };

                        t_r = u_r >= 0.0f ? t_r : FLT_MAX;
                        t_g = u_g >= 0.0f ? t_g : FLT_MAX;
                        t_b = u_b >= 0.0f ? t_b : FLT_MAX;

                        t += std::fmin(t_r, std::fmin(t_g, t_b));
                    }
                }
            }

            return t;
        }

        float FindGamutIntersection(float a, float b, float L1, float C1, float L0)
        {
            // Find the cusp of the gamut triangle
            const LC cusp{ FindCusp(a, b) };

            return FindGamutIntersection(a, b, L1, C1, L0, cusp);
        }

        // Rounded, not truncated, which would darken every color
        int ChannelToByte(float x)
        {
            return static_cast<int>(Clamp01(x) * 255.0f + 0.5f);
        }

        bool HexToEncodedSrgb(const char* text, EncodedSrgb* encoded, float* alpha, HexForms forms)
        {
            if (text == nullptr || encoded == nullptr)
            {
                return false;
            }

            while (IsBlank(*text))
            {
                ++text;
            }
            if (*text == '#')
            {
                ++text;
            }

            int digits[8]{};
            int count{ 0 };
            while (count < 8 && HexDigitValue(text[count]) >= 0) // Stops at '\0', never reads past it
            {
                digits[count] = HexDigitValue(text[count]);
                ++count;
            }
            for (const char* rest{ text + count }; *rest != '\0'; ++rest)
            {
                if (!IsBlank(*rest)) // Also rejects a ninth digit
                {
                    return false;
                }
            }

            const bool isShort{ count == 3 || count == 4 };
            const bool isLong{ count == 6 || count == 8 };
            const bool hasAlpha{ count == 4 || count == 8 };
            if ((!isLong && !(isShort && forms == HexForms::All)) || (hasAlpha && alpha == nullptr))
            {
                return false;
            }

            int bytes[4]{ 0, 0, 0, 255 };
            for (int i{ 0 }; i < (hasAlpha ? 4 : 3); ++i)
            {
                // A short form repeats each digit: #ABC is #AABBCC
                bytes[i] = isShort ? digits[i] * 17 : digits[i * 2] * 16 + digits[i * 2 + 1];
            }

            *encoded = { static_cast<float>(bytes[0]) / 255.0f,
                         static_cast<float>(bytes[1]) / 255.0f,
                         static_cast<float>(bytes[2]) / 255.0f };
            if (alpha != nullptr)
            {
                *alpha = static_cast<float>(bytes[3]) / 255.0f;
            }
            return true;
        }

        ST ToST(const LC& cusp)
        {
            return { cusp.C / cusp.L, cusp.C / (1.0f - cusp.L) };
        }

        ST GetSTMid(float a_, float b_)
        {
            const float S{ 0.11516993f + 1.0f / (
                +7.44778970f + 4.15901240f * b_
                + a_ * (-2.19557347f + 1.75198401f * b_
                    + a_ * (-2.13704948f - 10.02301043f * b_
                        + a_ * (-4.24894561f + 5.38770819f * b_ + 4.69891013f * a_
                            )))
                ) };

            const float T{ 0.11239642f + 1.0f / (
                +1.61320320f - 0.68124379f * b_
                + a_ * (+0.40370612f + 0.90148123f * b_
                    + a_ * (-0.27087943f + 0.61223990f * b_
                        + a_ * (+0.00299215f - 0.45399568f * b_ - 0.14661872f * a_
                            )))
                ) };

            return { S, T };
        }

        Cs GetCs(float L, float a_, float b_)
        {
            const LC cusp{ FindCusp(a_, b_) };

            const float C_max{ FindGamutIntersection(a_, b_, L, 1.0f, L, cusp) };
            const ST ST_max{ ToST(cusp) };

            // Scale factor to compensate for the curved part of the gamut shape
            const float k{ C_max / std::fmin((L * ST_max.S), (1.0f - L) * ST_max.T) };

            float C_mid{ 0.0f };
            {
                const ST ST_mid{ GetSTMid(a_, b_) };

                // Soft minimum instead of a sharp triangle shape, for a smooth chroma
                const float C_a{ L * ST_mid.S };
                const float C_b{ (1.0f - L) * ST_mid.T };
                C_mid = 0.9f * k * std::sqrt(std::sqrt(1.0f / (1.0f / (C_a * C_a * C_a * C_a) + 1.0f / (C_b * C_b * C_b * C_b))));
            }

            float C_0{ 0.0f };
            {
                // For C_0 the shape is independent of hue, so ST are constant:
                // values picked to roughly be the average ST
                const float C_a{ L * 0.4f };
                const float C_b{ (1.0f - L) * 0.8f };

                // Soft minimum instead of a sharp triangle shape, for a smooth chroma
                C_0 = std::sqrt(1.0f / (1.0f / (C_a * C_a) + 1.0f / (C_b * C_b)));
            }

            return { C_0, C_mid, C_max };
        }

        LinearSrgb ScaleToLargestChannel(const LinearSrgb& linear)
        {
            const float largest{ std::fmax(std::fmax(linear.r, linear.g), linear.b) };
            return { linear.r / largest, linear.g / largest, linear.b / largest };
        }
    }
}