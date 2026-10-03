// ImOk: perceptual (Oklab) color editing for Dear ImGui
// See LICENSE.txt for copyright and licensing details (standard MIT License).
// The color math is in imok_color.h, with no ImGui dependency.
//
// Usage:
//   ImOk::ColorEdit3("Text color", textColorEncoded, ImOkStoredAs_EncodedSrgb); // ImGui's convention
//   ImOk::ColorEdit3("Light color", lightColorLinear, ImOkStoredAs_LinearSrgb);
//   ImOk::ColorEdit4("Tint", tintLinear, ImOkStoredAs_LinearSrgb);
//
// ImGui's colors are encoded sRGB; lighting math needs linear sRGB. ImOkStoredAs says which one your
// floats hold. imok_color.h explains the color spaces.
//
// Pickers are fine meshes, and a window with several can pass 65536 vertices. With 16-bit
// ImDrawIdx your backend must set ImGuiBackendFlags_RendererHasVtxOffset (most official ones do),
// or define ImDrawIdx as unsigned int in imconfig.h. Otherwise ImGui::Render() asserts.

#pragma once

#include "imgui.h"
#include "imok_color.h"

#define IMOK_VERSION "0.1.0"

#if IMGUI_VERSION_NUM < 19261
#error "ImOk requires Dear ImGui 1.92.6 or newer."
#endif
// Flags for ImOk's drawing helpers
enum ImOkDrawFlags_
{
    ImOkDrawFlags_None            = 0,
    ImOkDrawFlags_ClampOutOfGamut = 1 << 0, // Clamp per channel instead of clipping hue-preserving (shifts hues)
};
typedef int ImOkDrawFlags; // -> enum ImOkDrawFlags_

// What your col[0..2] floats hold, in [0, 1]. No default: a wrong guess gives a wrong color
// with no error. Alpha (col[3]) is stored as-is, never encoded. Straight alpha: premultiply the
// linear sRGB color at upload.
enum ImOkStoredAs : int
{
    ImOkStoredAs_EncodedSrgb, // ImGui's convention, hex, 8-bit data
    ImOkStoredAs_LinearSrgb,  // Linear sRGB: engine material and light colors
};

// What an intensity number means. No default, as ImOkStoredAs: 800 is a bulb in lumens and a
// dim room in lux. ImOk labels and ranges the value; your engine converts it.
enum ImOkLightUnit : int
{
    ImOkLightUnit_Unitless, // A multiplier your engine defines
    ImOkLightUnit_Lumen,    // Total light from a source
    ImOkLightUnit_Candela,  // Light per direction
    ImOkLightUnit_Lux,      // Light arriving on a surface
    ImOkLightUnit_Nits,     // Brightness of a surface (cd/m2)
    ImOkLightUnit_EV100,    // Exposure value at ISO 100, log2: nits = 2^(EV - 3)
    ImOkLightUnit_COUNT
};

// What a light widget edits. Sets which units are valid (asserted; Unitless always is) and the
// hover text. Converting lumens to candela or nits needs the light's shape, which ImOk doesn't have.
enum ImOkLightKind : int
{
    ImOkLightKind_Point,       // Lumen, Candela, EV100
    ImOkLightKind_Spot,        // Lumen, Candela, EV100
    ImOkLightKind_Directional, // Lux
    ImOkLightKind_Area,        // Lumen, Nits, EV100
    ImOkLightKind_Emissive,    // Nits, EV100
    ImOkLightKind_COUNT
};

// Flags for the light widgets
enum ImOkLightEditFlags_
{
    ImOkLightEditFlags_None          = 0,
    ImOkLightEditFlags_NoReferences  = 1 << 0, // No reference ticks on the bars, no list on hover
    ImOkLightEditFlags_NoSpeedTweaks = 1 << 1, // No Alt/Shift speed change on the bars
    ImOkLightEditFlags_NoDragDrop    = 1 << 2, // LightEdit, LightPicker: no drag source or drop target
    ImOkLightEditFlags_NoTooltip     = 1 << 3, // LightEdit, LightPicker: no tooltip on the swatches
};
typedef int ImOkLightEditFlags; // -> enum ImOkLightEditFlags_

// Flags for ColorEdit3/4 and ColorPicker3/4. Display and Picker set what a widget starts with;
// right-click changes them, per widget.
enum ImOkColorEditFlags_
{
    ImOkColorEditFlags_None          = 0,

    // ColorEdit3/4's inline fields. At most one (asserted); default: the picker's own values.
    ImOkColorEditFlags_DisplayOkhsv  = 1 << 0,
    ImOkColorEditFlags_DisplayOkhsl  = 1 << 1,
    ImOkColorEditFlags_DisplayRgb    = 1 << 2, // The floats as stored
    ImOkColorEditFlags_DisplayHex    = 1 << 3, // Always encoded sRGB; AA is the alpha byte
    ImOkColorEditFlags_DisplayOkLCh  = 1 << 4, // CSS oklch(), read-only

    // The picker of ColorPicker3/4 and ColorEdit3/4's popup. At most one (asserted); default: Okhsv.
    ImOkColorEditFlags_PickerOkhsv   = 1 << 8, // Saturation x value square, hue bar
    ImOkColorEditFlags_PickerOkhsl   = 1 << 9, // Saturation x lightness square, hue bar

    ImOkColorEditFlags_NoDragDrop    = 1 << 16, // No drag source or drop target, e.g. inside a larger target

    // For light and emissive colors whose brightness is a separate value: the largest linear sRGB
    // channel is always 1, picked on hue x saturation (Okhsv at v = 1). Darker input is raised to
    // it; a darker col set from outside is shown raised and written only on an edit. RGB fields
    // are read-only. Not with PickerOkhsl or DisplayOkhsl (asserted).
    ImOkColorEditFlags_NoBrightness  = 1 << 17,

    ImOkColorEditFlags_NoSpeedTweaks = 1 << 18, // No Alt/Shift speed change on squares and bars
    ImOkColorEditFlags_NoTooltip     = 1 << 19, // No tooltip on the swatches
    ImOkColorEditFlags_NoInputs      = 1 << 20, // ColorEdit3/4: swatch and label only. Unlike ImGui's, keeps the right-click menu
    ImOkColorEditFlags_NoOptions     = 1 << 21, // No right-click menu (ColorEdit's popup keeps its own)

    // [Internal] Masks
    ImOkColorEditFlags_DisplayMask_  = ImOkColorEditFlags_DisplayOkhsv | ImOkColorEditFlags_DisplayOkhsl
                                     | ImOkColorEditFlags_DisplayRgb | ImOkColorEditFlags_DisplayHex
                                     | ImOkColorEditFlags_DisplayOkLCh,
    ImOkColorEditFlags_PickerMask_   = ImOkColorEditFlags_PickerOkhsv | ImOkColorEditFlags_PickerOkhsl,
};
typedef int ImOkColorEditFlags; // -> enum ImOkColorEditFlags_

namespace ImOk {
    // Demo window, defined in imok_demo.cpp (optional)
    void ShowDemoWindow(bool* open = nullptr);


    // --- ImGui colors ----------------------------------------------------------
    //
    // ImGui's colors (ImU32, ImVec4, style colors) are encoded sRGB. Linear sRGB values cross into
    // and out of them through these four functions.

    // Out of gamut: clipped keeping hue (ClipToSrgbGamut), or clamped per channel with
    // ImOkDrawFlags_ClampOutOfGamut; overshoot under 0.001 is always clamped. ImU32 rounds to the
    // nearest byte, ImVec4 keeps full precision. style.Alpha is not applied.
    ImU32 LinearSrgbToImU32(const LinearSrgb& linear, float alpha = 1.0f, ImOkDrawFlags flags = 0);
    ImVec4 LinearSrgbToImVec4(const LinearSrgb& linear, float alpha = 1.0f, ImOkDrawFlags flags = 0);

    // Decoded, not clamped. Alpha is not read.
    LinearSrgb ImVec4ToLinearSrgb(const ImVec4& encoded);
    LinearSrgb ImU32ToLinearSrgb(ImU32 encoded);


    // --- Gradients -------------------------------------------------------------
    //
    // Left to right, interpolated in the named space. For encoded sRGB, the naive one, use
    // ImDrawList::AddRectFilledMultiColor. Colors are multiplied by style.Alpha, as in ImGui's own
    // drawing. Colors outside the sRGB gamut are clipped at each vertex, so a path that crosses
    // the gamut edge shows sharp bends.

    // Physically correct, visually uneven. Stays inside the sRGB gamut.
    void AddRectGradientLinearSrgb(ImDrawList* drawList, const ImVec2& pMin, const ImVec2& pMax,
                                   const LinearSrgb& left, const LinearSrgb& right, ImOkDrawFlags flags = 0);

    // Visually even. Can leave the sRGB gamut between distant colors.
    void AddRectGradientOkLab(ImDrawList* drawList, const ImVec2& pMin, const ImVec2& pMax,
                              const OkLab& left, const OkLab& right, ImOkDrawFlags flags = 0);

    // Keeps chroma, sweeping the hues in between. Often leaves the sRGB gamut.
    void AddRectGradientOkLCh(ImDrawList* drawList, const ImVec2& pMin, const ImVec2& pMax,
                              const OkLCh& left, const OkLCh& right, HueDirection direction,
                              ImOkDrawFlags flags = 0);


    // --- Picker surfaces -------------------------------------------------------
    //
    // The pieces ImOk's pickers are drawn with. Each is a mesh of up to 16641 vertices (see the
    // renderer note at the top). Colors are multiplied by style.Alpha, as in the gradients.

    // Okhsv at a fixed hue (degrees): s from 0 (left) to 1 (right), v from 1 (top) to 0.
    void AddRectOkhsvSaturationValue(ImDrawList* drawList, const ImVec2& pMin, const ImVec2& pMax,
                                     float hue, ImOkDrawFlags flags = 0);

    // Okhsl at a fixed hue (degrees): s from 0 (left) to 1 (right), l from 1 (top) to 0. Each row
    // has one perceived lightness.
    void AddRectOkhslSaturationLightness(ImDrawList* drawList, const ImVec2& pMin, const ImVec2& pMax,
                                         float hue, ImOkDrawFlags flags = 0);

    // Okhsv at v = 1, NoBrightness's picker: hue from 0 (left) to 360 (right), s from 1 (top) to
    // 0 (white). The largest linear sRGB channel is always 1. Sharp edges near the top at red,
    // green and blue: the corners of the sRGB gamut.
    void AddRectOkhsvHueSaturation(ImDrawList* drawList, const ImVec2& pMin, const ImVec2& pMax,
                                   ImOkDrawFlags flags = 0);

    // Vertical, hue 0 (top) to 360 (bottom): a vivid color at each exact hue (Ottosson's strip).
    void AddRectHueBar(ImDrawList* drawList, const ImVec2& pMin, const ImVec2& pMax,
                       ImOkDrawFlags flags = 0);

    // Vertical, alpha 1 (top) to 0 (bottom), over a checkerboard. Blended in encoded sRGB, as all
    // ImGui UI (see ColorEdit4).
    void AddRectAlphaBar(ImDrawList* drawList, const ImVec2& pMin, const ImVec2& pMax,
                         const LinearSrgb& color, ImOkDrawFlags flags = 0);
    void AddRectAlphaBar(ImDrawList*, const ImVec2&, const ImVec2&, std::initializer_list<float>,
                         ImOkDrawFlags = 0) = delete; // Write LinearSrgb{ r, g, b }


    // --- Widgets ---------------------------------------------------------------
    //
    // Three independent choices, all converting through Oklab:
    //   STORAGE   What your floats hold (ImOkStoredAs)
    //   PICKER    How you navigate: Okhsv or Okhsl square plus hue bar (ImOkColorEditFlags_Picker*)
    //   DISPLAY   Which numbers the fields show (ImOkColorEditFlags_Display*)
    //
    // Two families:
    //   Stored RGB (ColorEdit3/4, ColorPicker3/4): your float[3] or float[4], plus ImOkStoredAs.
    //   Native perceptual (ColorPickerOkhsv/Okhsl): an Okhsv or Okhsl value itself.
    //
    // Width follows the item width. Each widget returns true on frames where the value changed.
    // Alt/Shift on a square or bar drags 100x slower or 10x faster, as on ImGui's drags.
    //
    // Drag and drop sends encoded sRGB at full float precision, as ImGui does, and a drop is
    // converted to the target's storage. ImGui's payloads carry no color space: linear sRGB floats
    // kept in an ImGui::ColorEdit3 arrive too dark, and nothing can detect it. Disabled widgets
    // take no drops.
    //
    // Undo: commit on IsItemDeactivatedAfterEdit(), and on a true return while !IsItemActive()
    // (a drop, which marks no edit, as in ImGui).

    // Edits an Okhsv value. Hue and saturation are kept at grays and black. alpha: optional, adds
    // an alpha bar. Flags: NoDragDrop and NoSpeedTweaks only (asserted). The label is the ID only.
    bool ColorPickerOkhsv(const char* label, Okhsv* color, float* alpha = nullptr, ImOkColorEditFlags flags = 0);

    // As ColorPickerOkhsv, for an Okhsl value. White keeps hue and saturation too.
    bool ColorPickerOkhsl(const char* label, Okhsl* color, float* alpha = nullptr, ImOkColorEditFlags flags = 0);

    // Square and hue bar (Okhsv by default; right-click to switch or to copy). Hue and saturation
    // are kept at grays and black, typed and dropped ones included. A col changed from outside is
    // read anew. col is written only on a user edit, clamped to [0, 1]. The label is the ID only.
    bool ColorPicker3(const char* label, float col[3], ImOkStoredAs storage, ImOkColorEditFlags flags = 0);

    // Laid out as ImGui::ColorEdit3: fields, a swatch that opens the picker in a popup, the label.
    // Right-click to change display or picker, or to copy. Otherwise as ColorPicker3.
    bool ColorEdit3(const char* label, float col[3], ImOkStoredAs storage, ImOkColorEditFlags flags = 0);

    // ColorPicker3 plus an alpha bar. col[3] is alpha.
    bool ColorPicker4(const char* label, float col[4], ImOkStoredAs storage, ImOkColorEditFlags flags = 0);

    // ColorEdit3 plus an alpha field and bar. Swatches blend in encoded sRGB, as all ImGui UI;
    // engines blending in linear sRGB composite lighter (50% white over black: 128 here, 188
    // there). Judge translucency in your engine.
    bool ColorEdit4(const char* label, float col[4], ImOkStoredAs storage, ImOkColorEditFlags flags = 0);


    // --- Lights ------------------------------------------------------------------
    //
    // Color, temperature and intensity are three values your engine multiplies (see imok_color.h,
    // Color temperature). The color is ColorEdit3 with ImOkColorEditFlags_NoBrightness; its hue
    // still sets how much light passes (saturated blue: 7% of white's). LightEdit and LightPicker
    // combine all three; for a light with more (cone, radius), lay out the parts yourself.

    // Kelvin: a blackbody bar spaced evenly in Oklab, ticks at common light sources (listed on
    // hover), and a field. Clamped to [MIN_KELVIN, MAX_KELVIN] on edit; a value set from outside
    // is shown clamped, not written.
    bool TemperatureEdit(const char* label, float* kelvin, ImOkLightEditFlags flags = 0);

    // Intensity in a stated unit: a logarithmic bar (equal steps, equal ratios) with reference
    // ticks, and a field. Clamped to the unit's range on edit (Unitless: 0 to 10000); a value set
    // from outside is shown clamped, not written.
    bool IntensityEdit(const char* label, float* intensity, ImOkLightUnit unit, ImOkLightEditFlags flags = 0);

    // Vertical variants, as ImGui::VSliderFloat: high values at the top, the field below, the label
    // to the right. size: 0 on an axis for a default.
    bool VTemperatureEdit(const char* label, const ImVec2& size, float* kelvin, ImOkLightEditFlags flags = 0);
    bool VIntensityEdit(const char* label, const ImVec2& size, float* intensity, ImOkLightUnit unit, ImOkLightEditFlags flags = 0);

    // One light in one row: a swatch, IntensityEdit's bar and field, the label. The swatch shows
    // color x temperature without brightness and opens LightPicker in a popup; a color dropped on
    // it sets col. kelvin: nullptr for no temperature (not 6500 K, which is faintly pink). One item
    // for undo.
    bool LightEdit(const char* label, ImOkLightKind kind, float col[3], ImOkStoredAs storage, float* kelvin,
                   float* intensity, ImOkLightUnit unit, ImOkLightEditFlags flags = 0);

    // LightEdit's popup, inline: the color's picker, TemperatureEdit (unless kelvin is nullptr),
    // IntensityEdit, and how much of the intensity the color passes. The label is the ID only.
    bool LightPicker(const char* label, ImOkLightKind kind, float col[3], ImOkStoredAs storage, float* kelvin,
                     float* intensity, ImOkLightUnit unit, ImOkLightEditFlags flags = 0);
}