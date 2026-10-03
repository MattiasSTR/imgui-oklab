// ImOk color math
// See LICENSE.txt for copyright and licensing details (standard MIT License).
// No dependencies, not even Dear ImGui: usable in game runtime code.
//
// ---------------------------------------------------------------------------
// Every color value is in a space, and each space has one job:
//
//   STORAGE     EncodedSrgb   8-bit textures, hex codes, ImGui colors, the monitor. Packed by
//                             the sRGB transfer function (often called gamma) so 256 steps fall
//                             where the eye needs them. No math on it.
//   PHYSICS     LinearSrgb    Amounts of light: lighting, blending, filtering. The same color
//                             space as EncodedSrgb, without the transfer function.
//   PERCEPTION  OkLab, OkLCh  How colors look: gradients, picking, lighter or darker, palettes,
//               Okhsv, Okhsl  color distance. Never for light math.
//
// One path connects them, and every conversion below is a step on it:
//
//   EncodedSrgb <-> LinearSrgb <-> OkLab <-> OkLCh, Okhsv, Okhsl
//
// Decode once where colors enter, encode once where they leave. A missing or extra step gives a
// wrong color with no error, and the types can't tell what your floats hold: name them
// (albedoLinear).
//
// Further reading:
//   B. Ottosson, "A perceptual color space for image processing" (2020)
//   B. Ottosson, "Okhsv and Okhsl" (2021)
//   John Novak, "What every coder should know about gamma" (2016)
// ---------------------------------------------------------------------------
//
// Hue is in degrees, [0, 360), as in CSS oklch(). Grays have no hue: conversions return 0.
// Alpha is not part of any color here; it stays a separate value and is never encoded.

#pragma once

#include <initializer_list> // For the deleted overloads

namespace ImOk
{
    // Encoded by the sRGB transfer function, [0, 1]. Decode before any math.
    struct EncodedSrgb
    {
        float r{ 0.0f };
        float g{ 0.0f };
        float b{ 0.0f };
    };

    // Linear sRGB: 0.5 is half the light of 1.0. May leave [0, 1] (HDR, out of gamut); clamp
    // where you store.
    struct LinearSrgb
    {
        float r{ 0.0f };
        float g{ 0.0f };
        float b{ 0.0f };
    };

    // L lightness in [0, 1]; a green-red and b blue-yellow, roughly [-0.4, 0.4]. Equal distances
    // look about equally different.
    struct OkLab
    {
        float L{ 0.0f };
        float a{ 0.0f };
        float b{ 0.0f };
    };

    // Oklab in polar form, as CSS oklch(). C >= 0 (below ~0.37 for sRGB and P3 colors), h in
    // degrees. Changing h at fixed L and C can leave the sRGB gamut.
    struct OkLCh
    {
        float L{ 0.0f };
        float C{ 0.0f };
        float h{ 0.0f };
    };

    // Perceptual HSV: the sRGB gamut as a cylinder. h in degrees, s and v in [0, 1]. Results can
    // land a hair outside the sRGB gamut.
    struct Okhsv
    {
        float h{ 0.0f };
        float s{ 0.0f };
        float v{ 0.0f };
    };

    // Perceptual HSL, as approximate as Okhsv. h in degrees, s and l in [0, 1]; l is Toe(L), not
    // Oklab's L.
    struct Okhsl
    {
        float h{ 0.0f };
        float s{ 0.0f };
        float l{ 0.0f };
    };


    // --- Encoded <-> linear sRGB -----------------------------------------------
    //
    // The piecewise sRGB transfer function (IEC 61966-2-1), as in _SRGB formats and CSS; not a
    // pure 2.2 power. Not clamped: values outside [0, 1] keep their sign.

    LinearSrgb EncodedSrgbToLinearSrgb(const EncodedSrgb& encoded);
    EncodedSrgb LinearSrgbToEncodedSrgb(const LinearSrgb& linear);


    // --- Linear sRGB <-> Oklab -------------------------------------------------
    //
    // Not clamped: a color outside the sRGB gamut comes back outside [0, 1] (see ClipToSrgbGamut).

    OkLab LinearSrgbToOkLab(const LinearSrgb& linear);
    LinearSrgb OkLabToLinearSrgb(const OkLab& lab);


    // --- Oklab <-> OkLCh -------------------------------------------------------
    //
    // Grays (C below 1e-5) get h = 0. Any h is accepted back; it wraps.

    OkLCh OkLabToOkLCh(const OkLab& lab);
    OkLab OkLChToOkLab(const OkLCh& lch);


    // --- Lightness with a reference white -------------------------------------
    //
    // Ottosson's toe: Oklab L to Lr, lightness relative to a reference white, close to CIELab's
    // L*. Okhsv and Okhsl build on it. L for image processing, Lr for lightness shown to a person.

    float Toe(float L);
    float ToeInv(float Lr);


    // --- Oklab <-> Okhsv -------------------------------------------------------
    //
    // Black is (0, 0, 0), grays have h = 0 and s = 0, and v = 0 is black for any h and s. A color
    // outside the sRGB gamut gives values outside [0, 1].

    Okhsv OkLabToOkhsv(const OkLab& lab);
    OkLab OkhsvToOkLab(const Okhsv& hsv);


    // --- Oklab <-> Okhsl -------------------------------------------------------
    //
    // Black is (0, 0, 0), grays have h = 0 and s = 0; l <= 0 is black and l >= 1 white for any h
    // and s. A color outside the sRGB gamut gives values outside [0, 1].

    Okhsl OkLabToOkhsl(const OkLab& lab);
    OkLab OkhslToOkLab(const Okhsl& hsl);


    // --- sRGB gamut ------------------------------------------------------------

    // Every channel in [0, 1], boundary included
    bool IsInSrgbGamut(const LinearSrgb& linear);
    bool IsInSrgbGamut(std::initializer_list<float>) = delete; // Write LinearSrgb{ r, g, b }

    // How ClipToSrgbGamut moves a color inside: keeping hue, along a straight line in (L, C)
    // toward a point on the gray axis (Ottosson, "sRGB gamut clipping").
    enum class GamutClipMethod
    {
        PreserveLightness,      // Keep L, reduce C. Ottosson: gamut_clip_preserve_chroma
        ProjectToMidGray,       // Toward L = 0.5. Ottosson: gamut_clip_project_to_0_5
        ProjectToCuspLightness, // Toward the cusp's L. Ottosson: gamut_clip_project_to_L_cusp
        AdaptiveMidGray,        // Mostly keeps L, giving some up for C when very light or dark. Ottosson: gamut_clip_adaptive_L0_0_5
        AdaptiveCuspLightness,  // As AdaptiveMidGray, around the cusp's L. Ottosson: gamut_clip_adaptive_L0_L_cusp
    };

    // Colors inside, boundary included, are returned unchanged. adaptiveAlpha is used only by the
    // Adaptive methods (Ottosson's default: AdaptiveMidGray, 0.05). Accurate to ~5e-4, so the
    // result can land a hair outside [0, 1].
    LinearSrgb ClipToSrgbGamut(const LinearSrgb& linear, GamutClipMethod method, float adaptiveAlpha = 0.05f);
    LinearSrgb ClipToSrgbGamut(std::initializer_list<float>, GamutClipMethod, float = 0.05f) = delete; // Write LinearSrgb{ r, g, b }


    // --- Relative luminance ----------------------------------------------------

    // Y with white = 1: physical brightness, for exposure and WCAG contrast. Perceived lightness
    // is Oklab's L. Not clamped.
    float RelativeLuminance(const LinearSrgb& linear);
    float RelativeLuminance(std::initializer_list<float>) = delete; // Write LinearSrgb{ r, g, b }


    // --- Color temperature -----------------------------------------------------
    //
    // A blackbody's color: physics, not a white point. No temperature is white; 6500 K is faintly
    // pink, since D65 lies off the blackbody curve. White balance belongs to the camera; to make
    // 6500 K white anyway, divide by KelvinToLinearSrgb(6500).
    //
    // As a light it carries no brightness of its own. Normalize it, and let a color filter it, as
    // pbrt-v4 does:
    //     light = color * temperature / RelativeLuminance(temperature) * intensity

    constexpr float MIN_KELVIN{ 1000.0f };
    constexpr float MAX_KELVIN{ 15000.0f };

    // Linear sRGB with the largest channel 1. kelvin is clamped to [MIN_KELVIN, MAX_KELVIN].
    // Below ~1900 K the color is outside the sRGB gamut and is clipped keeping hue.
    LinearSrgb KelvinToLinearSrgb(float kelvin);


    // --- Hex codes -------------------------------------------------------------

    // "#RRGGBBAA" plus the terminator
    constexpr int HEX_BUFFER_SIZE{ 10 };

    // CSS hex codes, always encoded sRGB. Parsing accepts #RGB, #RGBA, #RRGGBB and #RRGGBBAA, '#'
    // optional, any case, surrounding spaces; on failure it returns false and writes nothing. No
    // AA gives alpha 1; AA with alpha == nullptr fails. Formatting writes uppercase "#RRGGBB", or
    // "#RRGGBBAA" with alpha, each channel clamped and rounded: clip out-of-gamut colors first.
    bool HexToEncodedSrgb(const char* text, EncodedSrgb* encoded, float* alpha = nullptr);
    void EncodedSrgbToHex(const EncodedSrgb& encoded, char out[HEX_BUFFER_SIZE], const float* alpha = nullptr);


    // --- Interpolation ---------------------------------------------------------
    //
    // t is not clamped. No encoded variant: blending encoded values mixes numbers, not light. With
    // alpha: premultiply, interpolate, then divide by the interpolated alpha, or a transparent
    // endpoint's color bleeds in.

    // Physically correct (fades, blending, lighting), visually uneven.
    LinearSrgb LerpLinearSrgb(const LinearSrgb& from, const LinearSrgb& to, float t);

    // Visually even. Between distant hues it passes through less saturated colors.
    OkLab LerpOkLab(const OkLab& from, const OkLab& to, float t);

    // Which way around the hue circle LerpOkLCh goes, as in CSS Color 4
    enum class HueDirection
    {
        Shorter,    // At most 180 degrees
        Longer,     // At least 180 degrees
        Increasing, // Wrapping from 360 to 0
        Decreasing, // Wrapping from 0 to 360
    };

    // Keeps chroma, sweeping the hues in between. A gray endpoint takes the other's hue, so red to
    // gray just desaturates. Result h in [0, 360).
    OkLCh LerpOkLCh(const OkLCh& from, const OkLCh& to, float t, HueDirection direction);
}