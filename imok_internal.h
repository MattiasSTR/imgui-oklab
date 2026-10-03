// ImOk widget internals
// See LICENSE.txt for copyright and licensing details (standard MIT License).
// The widgets' logic without ImGui calls, so tests run it without a context.
// Not part of the stable API: may change without notice.

#pragma once

#include "imok.h"
#include "imok_color_internal.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace ImOk
{
    namespace Internal
    {
        inline float Clamp01(float x)
        {
            return std::fmin(std::fmax(x, 0.0f), 1.0f);
        }

        // True for zero or one bit set. Here, not in imok.cpp's anonymous namespace: it is used only
        // in IM_ASSERTs, and in Release builds GCC and Clang would warn that it is unused.
        inline bool HasAtMostOneBit(int bits)
        {
            return (bits & (bits - 1)) == 0;
        }

        // A widget's stored floats, unclamped
        inline LinearSrgb StoredToLinearSrgb(const float col[3], ImOkStoredAs storage)
        {
            return (storage == ImOkStoredAs_EncodedSrgb)
                ? EncodedSrgbToLinearSrgb({ col[0], col[1], col[2] })
                : LinearSrgb{ col[0], col[1], col[2] };
        }

        inline EncodedSrgb StoredToEncodedSrgb(const float col[3], ImOkStoredAs storage)
        {
            return (storage == ImOkStoredAs_EncodedSrgb)
                ? EncodedSrgb{ col[0], col[1], col[2] }
            : LinearSrgbToEncodedSrgb({ col[0], col[1], col[2] });
        }

        // Clamped first: the pickers overshoot a hair
        inline void LinearSrgbToStored(const LinearSrgb& linear, ImOkStoredAs storage, float col[3])
        {
            const LinearSrgb clamped{ Clamp01(linear.r), Clamp01(linear.g), Clamp01(linear.b) };
            if (storage == ImOkStoredAs_EncodedSrgb)
            {
                const EncodedSrgb encoded{ LinearSrgbToEncodedSrgb(clamped) };
                col[0] = encoded.r;
                col[1] = encoded.g;
                col[2] = encoded.b;
            }
            else
            {
                col[0] = clamped.r;
                col[1] = clamped.g;
                col[2] = clamped.b;
            }
        }

        // Expects [0, 1] (hex, in-range payloads)
        inline void EncodedSrgbToStored(const EncodedSrgb& encoded, ImOkStoredAs storage, float col[3])
        {
            if (storage == ImOkStoredAs_EncodedSrgb)
            {
                col[0] = encoded.r;
                col[1] = encoded.g;
                col[2] = encoded.b;
            }
            else
            {
                const LinearSrgb decoded{ EncodedSrgbToLinearSrgb(encoded) };
                col[0] = Clamp01(decoded.r);
                col[1] = Clamp01(decoded.g);
                col[2] = Clamp01(decoded.b);
            }
        }

        // NoBrightness: raises col so its largest linear sRGB channel is 1. Black becomes white; a
        // col already there is left bit-exact. True if col changed.
        inline bool RaiseStoredToLargestChannel(float col[3], ImOkStoredAs storage)
        {
            const LinearSrgb linear{ StoredToLinearSrgb(col, storage) };
            const float largest{ std::fmax(std::fmax(linear.r, linear.g), linear.b) };
            if (largest == 1.0f)
            {
                return false;
            }
            const LinearSrgb raised{ (largest > 0.0f) ? ScaleToLargestChannel(linear) : LinearSrgb{ 1.0f, 1.0f, 1.0f } };
            LinearSrgbToStored(raised, storage, col);
            return true;
        }

        // --- Keeping a picker's value -------------------------------------------
        //
        // A new color (a drop, typed RGB or hex, Original) replaces only what it defines. A gray
        // keeps the hue, with s = 0; black keeps hue and s. White: Okhsv sets s = 0 (at v = 1 a kept
        // s is vivid), Okhsl keeps h and s as at black. Grays are found by chroma, not s: r = g = b
        // gives s ~2e-7 to 7e-7.
        inline Okhsv OkLabToOkhsvKeeping(const OkLab& lab, const Okhsv& current)
        {
            Okhsv result{ OkLabToOkhsv(lab) };
            if (result.v <= 0.0f)
            {
                result.h = current.h;
                result.s = current.s;
            }
            else if (OkLabToOkLCh(lab).C < ACHROMATIC_CHROMA)
            {
                result.h = current.h;
                result.s = 0.0f;
            }
            return result;
        }

        inline Okhsl OkLabToOkhslKeeping(const OkLab& lab, const Okhsl& current)
        {
            Okhsl result{ OkLabToOkhsl(lab) };
            if (result.l <= 0.0f || result.l >= 1.0f)
            {
                result.h = current.h;
                result.s = current.s;
            }
            else if (OkLabToOkLCh(lab).C < ACHROMATIC_CHROMA)
            {
                result.h = current.h;
                result.s = 0.0f;
            }
            return result;
        }

        // --- Picker surfaces -------------------------------------------------
        //
        // Both surfaces use Oklab's hue, so a value moves between them with its hue exact.

        enum class Surface : int
        {
            OkhsvSaturationValue,
            OkhslSaturationLightness,
            Count,
        };

        // Hue in degrees plus the square's two values (Okhsv: s, v; Okhsl: s, l)
        struct SurfaceValue
        {
            float h{ 0.0f };
            float x{ 0.0f };
            float y{ 0.0f };
        };

        inline OkLab SurfaceValueToOkLab(Surface surface, const SurfaceValue& value)
        {
            return (surface == Surface::OkhslSaturationLightness)
                ? OkhslToOkLab({ value.h, value.x, value.y })
                : OkhsvToOkLab({ value.h, value.x, value.y });
        }

        inline SurfaceValue OkLabToSurfaceValue(Surface surface, const OkLab& lab)
        {
            if (surface == Surface::OkhslSaturationLightness)
            {
                const Okhsl hsl{ OkLabToOkhsl(lab) };
                return { hsl.h, hsl.s, hsl.l };
            }
            const Okhsv hsv{ OkLabToOkhsv(lab) };
            return { hsv.h, hsv.s, hsv.v };
        }

        inline SurfaceValue OkLabToSurfaceValueKeeping(Surface surface, const OkLab& lab, const SurfaceValue& current)
        {
            if (surface == Surface::OkhslSaturationLightness)
            {
                const Okhsl hsl{ OkLabToOkhslKeeping(lab, { current.h, current.x, current.y }) };
                return { hsl.h, hsl.s, hsl.l };
            }
            const Okhsv hsv{ OkLabToOkhsvKeeping(lab, { current.h, current.x, current.y }) };
            return { hsv.h, hsv.s, hsv.v };
        }

        // Keeps what the color doesn't define, as a stored edit does. value serves as current:
        // Keeping reads only h and s, which mean the same on both surfaces.
        inline SurfaceValue ConvertSurfaceValue(Surface from, Surface to, const SurfaceValue& value)
        {
            if (from == to)
            {
                return value;
            }
            SurfaceValue converted{ OkLabToSurfaceValueKeeping(to, SurfaceValueToOkLab(from, value), value) };
            converted.h = value.h;
            return converted;
        }

        // --- Hue bar ---------------------------------------------------------
        //
        // Ottosson's decorative Okhsv strip, ported from HSLuv to Okhsl so each position is the
        // exact hue. Lightness follows each hue's natural lightness (light yellows, dark blues).

        constexpr float HUE_BAR_SATURATION{ 0.9f };
        constexpr float HUE_BAR_LIGHTNESS{ 0.65f };
        constexpr float HUE_BAR_LIGHTNESS_SIN{ 0.20f };
        constexpr float HUE_BAR_LIGHTNESS_COS{ 0.09f };

        inline LinearSrgb HueBarColor(float hue)
        {
            const float hRad{ hue * DEG_TO_RAD };
            const float lightness{ HUE_BAR_LIGHTNESS
                + HUE_BAR_LIGHTNESS_SIN * std::sin(hRad)
                - HUE_BAR_LIGHTNESS_COS * std::cos(hRad) };

            return OkLabToLinearSrgb(OkhslToOkLab({ hue, HUE_BAR_SATURATION, lightness }));
        }

        // --- Temperature scale -------------------------------------------------
        //
        // TemperatureEdit's bar, spaced by Oklab distance: accumulated distance at log-spaced
        // temperatures, built on first use.

        constexpr int TEMPERATURE_SCALE_SAMPLES{ 128 };

        struct TemperatureScale
        {
            float position[TEMPERATURE_SCALE_SAMPLES]{}; // 0 at MIN_KELVIN, 1 at MAX_KELVIN
        };

        inline float TemperatureSampleKelvin(int i)
        {
            const float t{ static_cast<float>(i) / static_cast<float>(TEMPERATURE_SCALE_SAMPLES - 1) };
            return MIN_KELVIN * std::pow(MAX_KELVIN / MIN_KELVIN, t);
        }

        inline TemperatureScale BuildTemperatureScale()
        {
            TemperatureScale scale{};
            OkLab previous{ LinearSrgbToOkLab(KelvinToLinearSrgb(TemperatureSampleKelvin(0))) };
            float total{ 0.0f };
            for (int i{ 1 }; i < TEMPERATURE_SCALE_SAMPLES; ++i)
            {
                const OkLab current{ LinearSrgbToOkLab(KelvinToLinearSrgb(TemperatureSampleKelvin(i))) };
                const float dL{ current.L - previous.L };
                const float da{ current.a - previous.a };
                const float db{ current.b - previous.b };
                total += std::sqrt(dL * dL + da * da + db * db);
                scale.position[i] = total;
                previous = current;
            }
            for (float& position : scale.position)
            {
                position /= total;
            }
            return scale;
        }

        inline const TemperatureScale& GetTemperatureScale()
        {
            static const TemperatureScale scale{ BuildTemperatureScale() };
            return scale;
        }

        // Kelvin to a bar position in [0, 1] and back. Both clamp to the range and interpolate
        // between samples.
        inline float KelvinToBarPosition(float kelvin)
        {
            const TemperatureScale& scale{ GetTemperatureScale() };
            const float clamped{ std::fmin(std::fmax(kelvin, MIN_KELVIN), MAX_KELVIN) };
            const float index{ std::log(clamped / MIN_KELVIN) / std::log(MAX_KELVIN / MIN_KELVIN)
                               * static_cast<float>(TEMPERATURE_SCALE_SAMPLES - 1) };
            const int i{ static_cast<int>(std::fmin(std::floor(index), static_cast<float>(TEMPERATURE_SCALE_SAMPLES - 2))) };
            const float f{ index - static_cast<float>(i) };
            return scale.position[i] + (scale.position[i + 1] - scale.position[i]) * f;
        }

        inline float BarPositionToKelvin(float position)
        {
            const TemperatureScale& scale{ GetTemperatureScale() };
            const float p{ Clamp01(position) };

            // Last sample at or below p; positions increase
            int low{ 0 };
            int high{ TEMPERATURE_SCALE_SAMPLES - 1 };
            while (high - low > 1)
            {
                const int middle{ (low + high) / 2 };
                if (scale.position[middle] <= p)
                {
                    low = middle;
                }
                else
                {
                    high = middle;
                }
            }
            const float span{ scale.position[high] - scale.position[low] };
            const float f{ (span > 0.0f) ? (p - scale.position[low]) / span : 0.0f };
            const float t{ (static_cast<float>(low) + f) / static_cast<float>(TEMPERATURE_SCALE_SAMPLES - 1) };
            return MIN_KELVIN * std::pow(MAX_KELVIN / MIN_KELVIN, t);
        }

        // --- Light units -------------------------------------------------------
        //
        // IntensityEdit's ranges and references. A range's tick sits at its geometric mean (EV100:
        // the plain mean).

        struct LightReference
        {
            float value{ 0.0f };         // Tick position; above maxValue: tooltip only
            const char* text{ nullptr };
        };

        struct LightUnitInfo
        {
            const char* symbol{ nullptr };
            const char* description{ nullptr }; // Tooltip header
            float minValue{ 0.0f };
            float maxValue{ 1.0f };
            float logMin{ 0.0f }; // Smallest value above 0 on a logarithmic bar; 0: linear bar
            const LightReference* references{ nullptr };
            int referenceCount{ 0 };
        };

        // The left share of a logarithmic bar that means 0 (off)
        constexpr float INTENSITY_ZERO_ZONE{ 0.03f };

        inline const LightUnitInfo& GetLightUnitInfo(ImOkLightUnit unit)
        {
            static const LightReference UNITLESS_REFERENCES[]{
                { 1.0f, "1: pbrt-v4's default scale" },
            };
            static const LightReference LUMEN_REFERENCES[]{
                { 12.57f, "Candle: 12.57 lm (HDRP)" },
                { 100.0f, "Small decorative light: < 100 lm (HDRP)" },
                { 245.0f, "Decorative lamp: 200-300 lm (HDRP)" },
                { 566.0f, "Ceiling lamp, regular room: 400-800 lm (HDRP)" },
                { 980.0f, "Ceiling lamp, large room: 800-1200 lm (HDRP)" },
                { 6325.0f, "Street light: 1000-40000 lm (HDRP)" },
            };
            static const LightReference CANDELA_REFERENCES[]{
                { 1.0f, "Wax candle: ~1 cd (HDRP)" },
                { 64.0f, "800 lm bulb as a point light: 64 cd (800 / 4 pi)" },
            };
            static const LightReference LUX_REFERENCES[]{
                { 0.002f, "Moonless clear night: 0.002 lx (Wikipedia, Lux)" },
                { 0.1f, "Full moon: 0.05-0.2 lx, at most ~0.3 (Kyba et al. 2017)" },
                { 3.4f, "Civil twilight, dark limit: 3.4 lx (Wikipedia, Lux)" },
                { 50.0f, "Living room: 50 lx (Wikipedia, Lux)" },
                { 400.0f, "Office: 320-500 lx (Wikipedia, Lux)" },
                { 1000.0f, "Overcast day: 1000 lx (Wikipedia, Lux; HDRP: 1000-2000)" },
                { 15811.0f, "Daylight, not direct sun: 10000-25000 lx (Wikipedia, Lux)" },
                { 56569.0f, "Direct sunlight: 32000-100000 lx (Wikipedia, Lux; HDRP: up to 120000)" },
            };
            static const LightReference NITS_REFERENCES[]{
                { 122.0f, "Computer display: 50-300 nits (Wikipedia, Luminance)" },
                { 203.0f, "HDR reference white: 203 nits (ITU-R BT.2408)" },
                { 452.0f, "Overcast sky: ~320-640 nits (1000-2000 lx / pi, uniform sky)" },
                { 1.6e9f, "Sun's disk: 1.6e9 nits, off the scale (Wikipedia, Luminance)" },
            };
            static const LightReference EV100_REFERENCES[]{
                { -10.0f, "Milky Way core: -11 to -9 EV (Wikipedia, Exposure value)" },
                { -2.5f, "Full-moon landscape: -3 to -2 EV (Wikipedia, Exposure value)" },
                { 6.0f, "Home interiors: 5-7 EV (Wikipedia, Exposure value)" },
                { 7.5f, "Offices: 7-8 EV (Wikipedia, Exposure value)" },
                { 12.0f, "Heavy overcast: 12 EV (Wikipedia, Exposure value)" },
                { 15.0f, "Full sunlight, sunny 16: 15 EV (Wikipedia, Exposure value)" },
            };
            static const LightUnitInfo INFOS[ImOkLightUnit_COUNT]{
                { "", "Unitless: a multiplier your engine defines", 0.0f, 10000.0f, 0.001f,
                  UNITLESS_REFERENCES, IM_COUNTOF(UNITLESS_REFERENCES) },
                { "lm", "Lumen: total light from a source", 0.0f, 1000000.0f, 0.1f,
                  LUMEN_REFERENCES, IM_COUNTOF(LUMEN_REFERENCES) },
                { "cd", "Candela: light per direction", 0.0f, 1000000.0f, 0.01f,
                  CANDELA_REFERENCES, IM_COUNTOF(CANDELA_REFERENCES) },
                { "lx", "Lux: light arriving on a surface", 0.0f, 150000.0f, 0.0001f,
                  LUX_REFERENCES, IM_COUNTOF(LUX_REFERENCES) },
                { "nits", "Nits: brightness of a surface (cd/m2)", 0.0f, 1000000.0f, 0.001f,
                  NITS_REFERENCES, IM_COUNTOF(NITS_REFERENCES) },
                { "EV", "EV100: exposure value at ISO 100, log2", -12.0f, 18.0f, 0.0f,
                  EV100_REFERENCES, IM_COUNTOF(EV100_REFERENCES) },
            };
            IM_ASSERT(unit >= 0 && unit < ImOkLightUnit_COUNT && "Invalid ImOkLightUnit.");
            return INFOS[unit];
        }

        struct TemperatureReferences
        {
            const LightReference* references{ nullptr };
            int count{ 0 };
        };

        // Wikipedia, Color temperature. The last is past MAX_KELVIN: tooltip only.
        inline TemperatureReferences GetTemperatureReferences()
        {
            static const LightReference REFERENCES[]{
                { 1700.0f, "Match flame, low-pressure sodium: 1700 K" },
                { 1850.0f, "Candle flame, sunrise and sunset: 1850 K" },
                { 2400.0f, "Standard incandescent lamp: 2400 K" },
                { 2700.0f, "Soft white fluorescent and LED lamps: 2700 K" },
                { 3000.0f, "Warm white fluorescent and LED lamps: 3000 K" },
                { 3200.0f, "Studio lamps, photofloods: 3200 K" },
                { 5000.0f, "Horizon daylight, cool white fluorescent: 5000 K" },
                { 5745.0f, "Vertical daylight, electronic flash: 5500-6000 K" },
                { 6500.0f, "Overcast daylight, daylight LED lamps: 6500 K" },
                { 7849.0f, "LCD and CRT screens: 6500-9500 K" },
                { 20125.0f, "Clear blue poleward sky: 15000-27000 K, off the scale" },
            };
            return { REFERENCES, IM_COUNTOF(REFERENCES) };
        }

        // An intensity to a bar position in [0, 1] and back, clamped to the unit's range. On a
        // logarithmic bar, values between 0 and logMin show at the zero zone's right edge.
        inline float IntensityToBarPosition(float intensity, const LightUnitInfo& info)
        {
            const float clamped{ std::fmin(std::fmax(intensity, info.minValue), info.maxValue) };
            if (info.logMin <= 0.0f)
            {
                return (clamped - info.minValue) / (info.maxValue - info.minValue);
            }
            if (clamped <= 0.0f)
            {
                return 0.0f;
            }
            const float t{ std::log(std::fmax(clamped, info.logMin) / info.logMin) / std::log(info.maxValue / info.logMin) };
            return INTENSITY_ZERO_ZONE + (1.0f - INTENSITY_ZERO_ZONE) * t;
        }

        inline float BarPositionToIntensity(float position, const LightUnitInfo& info)
        {
            const float p{ Clamp01(position) };
            if (info.logMin <= 0.0f)
            {
                return info.minValue + (info.maxValue - info.minValue) * p;
            }
            if (p < INTENSITY_ZERO_ZONE)
            {
                return 0.0f;
            }
            const float t{ (p - INTENSITY_ZERO_ZONE) / (1.0f - INTENSITY_ZERO_ZONE) };
            return std::fmin(info.logMin * std::pow(info.maxValue / info.logMin, t), info.maxValue);
        }

        // 0.002, 3.40, 450.0, 110000
        inline int IntensityDecimals(float intensity)
        {
            const float magnitude{ std::fabs(intensity) };
            return (magnitude >= 1000.0f) ? 0 : (magnitude >= 100.0f) ? 1 : (magnitude >= 1.0f) ? 2
                 : (magnitude >= 0.01f) ? 3 : 4;
        }

        // --- Light kinds -------------------------------------------------------

        constexpr int LightUnitBit(ImOkLightUnit unit)
        {
            return 1 << static_cast<int>(unit);
        }

        struct LightKindInfo
        {
            int validUnits{ 0 };         // LightUnitBit per valid unit
            const char* note{ nullptr }; // Tooltip line; not shown for Unitless
        };

        inline const LightKindInfo& GetLightKindInfo(ImOkLightKind kind)
        {
            constexpr int UNITLESS{ LightUnitBit(ImOkLightUnit_Unitless) };
            constexpr int LOCAL{ UNITLESS | LightUnitBit(ImOkLightUnit_Lumen) | LightUnitBit(ImOkLightUnit_Candela)
                                 | LightUnitBit(ImOkLightUnit_EV100) };
            static const LightKindInfo INFOS[ImOkLightKind_COUNT]{
                { LOCAL, "Point light: lumens = candela x 4 pi" },
                { LOCAL, "Spot light: lm to cd depends on the cone, and engines differ" },
                { UNITLESS | LightUnitBit(ImOkLightUnit_Lux), "Directional light: lux on a surface facing it" },
                { UNITLESS | LightUnitBit(ImOkLightUnit_Lumen) | LightUnitBit(ImOkLightUnit_Nits)
                    | LightUnitBit(ImOkLightUnit_EV100), "Area light: lumens = nits x pi x area, one side" },
                { UNITLESS | LightUnitBit(ImOkLightUnit_Nits) | LightUnitBit(ImOkLightUnit_EV100),
                  "Emissive surface: its brightness, in nits" },
            };
            IM_ASSERT(kind >= 0 && kind < ImOkLightKind_COUNT && "Invalid ImOkLightKind.");
            return INFOS[kind];
        }

        inline bool IsLightUnitValid(ImOkLightKind kind, ImOkLightUnit unit)
        {
            return (GetLightKindInfo(kind).validUnits & LightUnitBit(unit)) != 0;
        }

        // color x temperature, before normalization. kelvin nullptr: the color alone.
        inline LinearSrgb FilteredTemperature(const LinearSrgb& color, const float* kelvin)
        {
            const LinearSrgb temperature{ (kelvin != nullptr) ? KelvinToLinearSrgb(*kelvin) : LinearSrgb{ 1.0f, 1.0f, 1.0f } };
            return { color.r * temperature.r, color.g * temperature.g, color.b * temperature.b };
        }

        // Y(color x temperature) / Y(temperature): the share of the intensity the color passes.
        // Y(temperature) is at least blue's 0.0722, since its largest channel is 1.
        inline float LightColorTransmittance(const LinearSrgb& color, const float* kelvin)
        {
            const LinearSrgb temperature{ FilteredTemperature({ 1.0f, 1.0f, 1.0f }, kelvin) };
            return std::fmax(0.0f, RelativeLuminance(FilteredTemperature(color, kelvin)) / RelativeLuminance(temperature));
        }

        // Scaled by the transmittance; EV100 is shifted by its log2 instead
        inline float FilteredIntensity(float intensity, ImOkLightUnit unit, float transmittance)
        {
            IM_ASSERT(transmittance > 0.0f);
            return (unit == ImOkLightUnit_EV100) ? intensity + std::log2(transmittance) : intensity * transmittance;
        }

        // color x temperature at the largest channel 1. Black when nothing passes (pure blue
        // over 1000 K, whose blue is clipped to about 0).
        inline LinearSrgb LightSwatchColor(const LinearSrgb& color, const float* kelvin)
        {
            const LinearSrgb filtered{ FilteredTemperature(color, kelvin) };
            const float largest{ std::fmax(std::fmax(filtered.r, filtered.g), filtered.b) };
            return (largest > 0.0f) ? ScaleToLargestChannel(filtered) : LinearSrgb{};
        }

        // --- Drag and drop ------------------------------------------------------
        //
        // ImGui's color payloads are encoded sRGB floats with straight alpha, by convention only.

        inline bool IsFinitePayload(const float* payload, int count)
        {
            for (int i{ 0 }; i < count; ++i)
            {
                if (!std::isfinite(payload[i]))
                {
                    return false;
                }
            }
            return true;
        }

        inline bool IsPayloadInRange(const float* payload)
        {
            return payload[0] >= 0.0f && payload[0] <= 1.0f
                && payload[1] >= 0.0f && payload[1] <= 1.0f
                && payload[2] >= 0.0f && payload[2] <= 1.0f;
        }

        // Expects finite values. In [0, 1]: decoded and clamped, which only absorbs rounding
        // (clipping would move gamut-edge colors by ~5e-4). Outside: clipped keeping hue, since a
        // per-channel clamp turns an HDR orange yellow.
        inline LinearSrgb PayloadToLinearSrgb(const float* payload)
        {
            const LinearSrgb decoded{ EncodedSrgbToLinearSrgb({ payload[0], payload[1], payload[2] }) };
            if (IsPayloadInRange(payload))
            {
                return { Clamp01(decoded.r), Clamp01(decoded.g), Clamp01(decoded.b) };
            }
            return ClipToSrgbGamut(decoded, GamutClipMethod::AdaptiveMidGray);
        }

        // 4 floats set alpha, clamped; 3 floats or alpha == nullptr leave it, as in ImGui. True if
        // it changed.
        inline bool ApplyPayloadAlpha(const float* payload, int count, float* alpha)
        {
            if (count < 4 || alpha == nullptr)
            {
                return false;
            }
            const float dropped{ Clamp01(payload[3]) };
            if (dropped == *alpha)
            {
                return false;
            }
            *alpha = dropped;
            return true;
        }

        // Apart, since an alpha-only drop is no color edit
        struct DropResult
        {
            bool rgbChanged{ false };
            bool alphaChanged{ false };
        };

        // A non-finite value rejects the whole drop. Each part is written only if it changed.
        inline DropResult ApplyColorPayload(const float* payload, int count, ImOkStoredAs storage,
                                            float col[3], float* alpha)
        {
            DropResult result{};
            if (!IsFinitePayload(payload, count))
            {
                return result;
            }

            float dropped[3]{};
            if (IsPayloadInRange(payload))
            {
                EncodedSrgbToStored({ payload[0], payload[1], payload[2] }, storage, dropped);
            }
            else
            {
                LinearSrgbToStored(PayloadToLinearSrgb(payload), storage, dropped);
            }

            if (dropped[0] != col[0] || dropped[1] != col[1] || dropped[2] != col[2])
            {
                col[0] = dropped[0];
                col[1] = dropped[1];
                col[2] = dropped[2];
                result.rgbChanged = true;
            }
            result.alphaChanged = ApplyPayloadAlpha(payload, count, alpha);
            return result;
        }

        // --- Copy formats -------------------------------------------------------
        //
        // The "Copy as" rows. Floats read back bit-exact. Formatting follows the C locale, as
        // ImGui's own copy does.

        // Longest row: 4 floats at 9 digits plus separators, 76 characters. Longer is truncated.
        constexpr int COPY_BUFFER_SIZE{ 128 };

        // "-9.99999935e-41f" plus the terminator
        constexpr int FLOAT_LITERAL_BUFFER_SIZE{ 24 };

        // The shortest literal that strtof reads back bit-exact: "0.8f", "0.29803923f", "1.0f"
        // ("1f" doesn't compile), "1e-05f". NaN and infinity as printf writes them.
        inline void FloatToLiteral(float x, char* out, int size)
        {
            // From digits10: %g trims trailing zeros, so fewer digits never give a shorter form
            char digits[FLOAT_LITERAL_BUFFER_SIZE - 3]{}; // Room for ".0f"
            for (int precision{ std::numeric_limits<float>::digits10 };
                 precision <= std::numeric_limits<float>::max_digits10; ++precision)
            {
                std::snprintf(digits, sizeof(digits), "%.*g", precision, x);
                if (std::strtof(digits, nullptr) == x)
                {
                    break;
                }
            }

            const char* suffix{ !std::isfinite(x) ? ""
                                : (std::strpbrk(digits, ".e") != nullptr) ? "f" : ".0f" };
            std::snprintf(out, static_cast<size_t>(size), "%s%s", digits, suffix);
        }

        // "(a, b, c)" or "(a, b, c, d)"
        inline void FloatTupleText(const float* values, int count, char out[COPY_BUFFER_SIZE])
        {
            char literals[4][FLOAT_LITERAL_BUFFER_SIZE]{};
            for (int i{ 0 }; i < count; ++i)
            {
                FloatToLiteral(values[i], literals[i], FLOAT_LITERAL_BUFFER_SIZE);
            }
            if (count == 4)
            {
                std::snprintf(out, COPY_BUFFER_SIZE, "(%s, %s, %s, %s)", literals[0], literals[1], literals[2], literals[3]);
            }
            else
            {
                std::snprintf(out, COPY_BUFFER_SIZE, "(%s, %s, %s)", literals[0], literals[1], literals[2]);
            }
        }

        inline void EncodedSrgbToFloatsText(const EncodedSrgb& encoded, const float* alpha, char out[COPY_BUFFER_SIZE])
        {
            const float values[4]{ encoded.r, encoded.g, encoded.b, (alpha != nullptr) ? *alpha : 0.0f };
            FloatTupleText(values, (alpha != nullptr) ? 4 : 3, out);
        }

        inline void LinearSrgbToFloatsText(const LinearSrgb& linear, const float* alpha, char out[COPY_BUFFER_SIZE])
        {
            const float values[4]{ linear.r, linear.g, linear.b, (alpha != nullptr) ? *alpha : 0.0f };
            FloatTupleText(values, (alpha != nullptr) ? 4 : 3, out);
        }

        // "(204, 76, 51)", rounded and clamped as hex. Always encoded: 8-bit linear sRGB bands.
        inline void EncodedSrgbToBytesText(const EncodedSrgb& encoded, const float* alpha, char out[COPY_BUFFER_SIZE])
        {
            const int r{ ChannelToByte(encoded.r) };
            const int g{ ChannelToByte(encoded.g) };
            const int b{ ChannelToByte(encoded.b) };
            if (alpha != nullptr)
            {
                std::snprintf(out, COPY_BUFFER_SIZE, "(%d, %d, %d, %d)", r, g, b, ChannelToByte(*alpha));
            }
            else
            {
                std::snprintf(out, COPY_BUFFER_SIZE, "(%d, %d, %d)", r, g, b);
            }
        }

        // CSS Color 4: "oklch(58.60% 0.16761 33.00)", plus " / 0.500" with alpha. C at 5 decimals so
        // every 8-bit color reads back byte-exact. Grays get CSS's powerless hue, "none": a hue
        // would bend CSS gradients through it.
        inline void OkLChToCssText(const OkLCh& lch, const float* alpha, char out[COPY_BUFFER_SIZE])
        {
            char alphaText[32]{};
            if (alpha != nullptr)
            {
                std::snprintf(alphaText, sizeof(alphaText), " / %.3f", *alpha);
            }

            if (lch.C < ACHROMATIC_CHROMA)
            {
                std::snprintf(out, COPY_BUFFER_SIZE, "oklch(%.2f%% 0 none%s)", lch.L * 100.0f, alphaText);
            }
            else
            {
                std::snprintf(out, COPY_BUFFER_SIZE, "oklch(%.2f%% %.5f %.2f%s)", lch.L * 100.0f, lch.C, lch.h, alphaText);
            }
        }

        // Menu rows, in order
        enum class CopyFormat : int
        {
            StoredFloats, // col bit for bit
            OtherFloats,  // Decoded or encoded, unclamped
            Bytes,
            Hex,
            HexAlpha,     // 4-variants only
            OkLCh,
            Count,
        };

        inline const char* CopyFormatName(CopyFormat format, ImOkStoredAs storage)
        {
            const bool encoded{ storage == ImOkStoredAs_EncodedSrgb };
            switch (format)
            {
            case CopyFormat::StoredFloats: return encoded ? "Encoded sRGB floats (stored)" : "Linear sRGB floats (stored)";
            case CopyFormat::OtherFloats:  return encoded ? "Linear sRGB floats" : "Encoded sRGB floats";
            case CopyFormat::Bytes:        return "Encoded sRGB bytes";
            case CopyFormat::Hex:          return "Hex (encoded sRGB)";
            case CopyFormat::HexAlpha:     return "Hex with alpha";
            case CopyFormat::OkLCh:        return "CSS oklch()";
            case CopyFormat::Count:        break;
            }
            return "";
        }

        // False, with out empty, if the row doesn't apply (HexAlpha without alpha)
        inline bool CopyText(CopyFormat format, const float col[3], const float* alpha, ImOkStoredAs storage,
                             char out[COPY_BUFFER_SIZE])
        {
            out[0] = '\0';
            switch (format)
            {
            case CopyFormat::StoredFloats:
            {
                const float values[4]{ col[0], col[1], col[2], (alpha != nullptr) ? *alpha : 0.0f };
                FloatTupleText(values, (alpha != nullptr) ? 4 : 3, out);
                return true;
            }
            case CopyFormat::OtherFloats:
                if (storage == ImOkStoredAs_EncodedSrgb)
                {
                    LinearSrgbToFloatsText(StoredToLinearSrgb(col, storage), alpha, out);
                }
                else
                {
                    EncodedSrgbToFloatsText(StoredToEncodedSrgb(col, storage), alpha, out);
                }
                return true;
            case CopyFormat::Bytes:
                EncodedSrgbToBytesText(StoredToEncodedSrgb(col, storage), alpha, out);
                return true;
            case CopyFormat::Hex:
                EncodedSrgbToHex(StoredToEncodedSrgb(col, storage), out);
                return true;
            case CopyFormat::HexAlpha:
                if (alpha == nullptr)
                {
                    return false;
                }
                EncodedSrgbToHex(StoredToEncodedSrgb(col, storage), out, alpha);
                return true;
            case CopyFormat::OkLCh:
                OkLChToCssText(OkLabToOkLCh(LinearSrgbToOkLab(StoredToLinearSrgb(col, storage))), alpha, out);
                return true;
            case CopyFormat::Count:
                break;
            }
            return false;
        }

        // --- Swatch tooltip -----------------------------------------------------
        //
        // The text is col, as copy is, even where the swatch shows it raised (NoBrightness).

        // 4 decimals: 3 would merge 19 of the 256 dark linear sRGB byte levels
        struct ColorTooltipText
        {
            char stored[COPY_BUFFER_SIZE]{}; // "Linear sRGB (stored): 0.6038, 0.0723, 0.0331"
            char hex[HEX_BUFFER_SIZE]{};     // "#CC4C33", "#CC4C3380" with alpha
            char oklch[COPY_BUFFER_SIZE]{};
        };

        inline ColorTooltipText ColorTooltipLines(const float col[3], const float* alpha, ImOkStoredAs storage)
        {
            ColorTooltipText text{};
            const char* space{ (storage == ImOkStoredAs_EncodedSrgb) ? "Encoded sRGB" : "Linear sRGB" };
            if (alpha != nullptr)
            {
                std::snprintf(text.stored, sizeof(text.stored), "%s (stored): %.4f, %.4f, %.4f, %.4f",
                              space, col[0], col[1], col[2], *alpha);
            }
            else
            {
                std::snprintf(text.stored, sizeof(text.stored), "%s (stored): %.4f, %.4f, %.4f",
                              space, col[0], col[1], col[2]);
            }
            EncodedSrgbToHex(StoredToEncodedSrgb(col, storage), text.hex, alpha);
            CopyText(CopyFormat::OkLCh, col, alpha, storage, text.oklch);
            return text;
        }

        // "The color passes 41.2%: 330.0 lm". EV100 is shifted by log2 of the share.
        inline void LightPassesText(const LinearSrgb& color, const float* kelvin, float intensity, ImOkLightUnit unit,
                                    char out[COPY_BUFFER_SIZE])
        {
            const float transmittance{ LightColorTransmittance(color, kelvin) };
            if (transmittance <= 0.0f)
            {
                std::snprintf(out, COPY_BUFFER_SIZE, "The color passes none of this temperature");
                return;
            }
            const LightUnitInfo& info{ GetLightUnitInfo(unit) };
            const float passed{ FilteredIntensity(intensity, unit, transmittance) };
            std::snprintf(out, COPY_BUFFER_SIZE, "The color passes %.3g%%: %.*f%s%s", transmittance * 100.0f,
                          IntensityDecimals(passed), passed, (info.symbol[0] != '\0') ? " " : "", info.symbol);
        }

        // No floats: color x temperature is no stored value, and copying it would bake the temperature in
        struct LightTooltipText
        {
            char shows[64]{};                // "Color x 2700 K, brightness not shown"
            char passes[COPY_BUFFER_SIZE]{};
        };

        inline LightTooltipText LightTooltipLines(const LinearSrgb& color, const float* kelvin, float intensity,
                                                  ImOkLightUnit unit)
        {
            LightTooltipText text{};
            if (kelvin != nullptr)
            {
                std::snprintf(text.shows, sizeof(text.shows), "Color x %.0f K, brightness not shown", *kelvin);
            }
            else
            {
                std::snprintf(text.shows, sizeof(text.shows), "Color, brightness not shown");
            }
            LightPassesText(color, kelvin, intensity, unit, text.passes);
            return text;
        }
    }
}