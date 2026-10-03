// ImOk: Dear ImGui drawing helpers and widgets
// See LICENSE.txt for copyright and licensing details (standard MIT License).

#include "imok.h"
#include "imok_internal.h"
// For MarkItemEdited only (MarkEdited)
#include "imgui_internal.h"
#include <cmath>
#include <cstring>
#include <cstdio>

namespace ImOk
{
    namespace
    {
        // Finer than the squares: a bar's vertices grow with its length, not its area
        constexpr float GRADIENT_SEGMENT_WIDTH{ 1.0f };
        constexpr int GRADIENT_MAX_SEGMENTS{ 4096 };

        // Mirrored in test_draw.cpp
        constexpr float SURFACE_CELL_SIZE{ 2.0f };
        constexpr int SURFACE_MAX_CELLS{ 128 };

        // 360 would wrap to 0 and jump the marker to the top
        constexpr float MAX_HUE{ 359.99f };


        constexpr float KELVIN_DRAG_SPEED{ 10.0f };

        constexpr float INTENSITY_BAR_DARKEST{ 0.15f };
        constexpr float INTENSITY_DRAG_FRACTION{ 0.01f };
        constexpr float INTENSITY_DRAG_MIN_STEPS{ 10.0f };
        constexpr float EV_DRAG_SPEED{ 0.02f };

        constexpr float VERTICAL_LIGHT_DEFAULT_FRAMES{ 8.0f };

        constexpr float LIGHT_POPUP_WIDTH_FRAMES{ 14.0f };

        // Far wider than float rounding at blue's hue, far narrower than a cell
        constexpr float PRIMARY_HUE_MARGIN{ 0.05f };

        using Internal::Clamp01;
        using Internal::HasAtMostOneBit;
        using Internal::StoredToLinearSrgb;
        using Internal::LinearSrgbToStored;
        using Internal::ChannelToByte;

        float StyleAlpha()
        {
            return ImGui::GetStyle().Alpha;
        }

        // Clamped, not clipped: clipping keeping hue moves such colors far near blue's hue
        constexpr float DISPLAY_CLAMP_TOLERANCE{ 0.001f };

        bool IsNearSrgbGamut(const LinearSrgb& linear)
        {
            const float low{ -DISPLAY_CLAMP_TOLERANCE };
            const float high{ 1.0f + DISPLAY_CLAMP_TOLERANCE };
            return linear.r >= low && linear.r <= high && linear.g >= low && linear.g <= high
                && linear.b >= low && linear.b <= high;
        }

        // The final clamp also absorbs the clip's residual
        EncodedSrgb LinearSrgbToDisplayEncoded(const LinearSrgb& linear, ImOkDrawFlags flags)
        {
            const bool clamp{ (flags & ImOkDrawFlags_ClampOutOfGamut) != 0 || IsNearSrgbGamut(linear) };
            const LinearSrgb inGamut{ clamp ? linear : ClipToSrgbGamut(linear, GamutClipMethod::AdaptiveMidGray) };
            const EncodedSrgb encoded{ LinearSrgbToEncodedSrgb(inGamut) };
            return { Clamp01(encoded.r), Clamp01(encoded.g), Clamp01(encoded.b) };
        }

        typedef LinearSrgb (*GradientColorFn)(float t, const void* userData);

        enum class GradientAxis
        {
            Horizontal,
            Vertical,
        };

        void AddRectGradient(ImDrawList* drawList, const ImVec2& pMin, const ImVec2& pMax, GradientAxis axis,
                             GradientColorFn colorAt, const void* userData, ImOkDrawFlags flags)
        {
            const float width{ pMax.x - pMin.x };
            const float height{ pMax.y - pMin.y };
            if (width <= 0.0f || height <= 0.0f)
            {
                return;
            }

            const float length{ axis == GradientAxis::Horizontal ? width : height };
            const int segments{ static_cast<int>(std::fmin(std::ceil(length / GRADIENT_SEGMENT_WIDTH),
                                                           static_cast<float>(GRADIENT_MAX_SEGMENTS))) };
            const ImVec2 uv{ ImGui::GetFontTexUvWhitePixel() };

            drawList->PrimReserve(segments * 6, (segments + 1) * 2);
            const ImDrawIdx base{ static_cast<ImDrawIdx>(drawList->_VtxCurrentIdx) };

            for (int i{ 0 }; i <= segments; ++i)
            {
                const float t{ static_cast<float>(i) / static_cast<float>(segments) };
                const ImU32 color{ LinearSrgbToImU32(colorAt(t, userData), StyleAlpha(), flags) };

                if (axis == GradientAxis::Horizontal)
                {
                    const float x{ pMin.x + width * t };
                    drawList->PrimWriteVtx(ImVec2(x, pMin.y), uv, color);
                    drawList->PrimWriteVtx(ImVec2(x, pMax.y), uv, color);
                }
                else
                {
                    const float y{ pMin.y + height * t };
                    drawList->PrimWriteVtx(ImVec2(pMin.x, y), uv, color);
                    drawList->PrimWriteVtx(ImVec2(pMax.x, y), uv, color);
                }
            }

            for (int i{ 0 }; i < segments; ++i)
            {
                const ImDrawIdx edgeA0{ static_cast<ImDrawIdx>(base + i * 2) };
                const ImDrawIdx edgeA1{ static_cast<ImDrawIdx>(edgeA0 + 1) };
                const ImDrawIdx edgeB0{ static_cast<ImDrawIdx>(edgeA0 + 2) };
                const ImDrawIdx edgeB1{ static_cast<ImDrawIdx>(edgeA0 + 3) };

                drawList->PrimWriteIdx(edgeA0);
                drawList->PrimWriteIdx(edgeA1);
                drawList->PrimWriteIdx(edgeB1);
                drawList->PrimWriteIdx(edgeA0);
                drawList->PrimWriteIdx(edgeB1);
                drawList->PrimWriteIdx(edgeB0);
            }
        }

        typedef LinearSrgb (*SurfaceColorFn)(float u, float v, const void* userData);

        // extraColumns: ascending u in (0, 1) where the surface has a corner no cell may straddle
        void AddRectSurface(ImDrawList* drawList, const ImVec2& pMin, const ImVec2& pMax,
                            SurfaceColorFn colorAt, const void* userData, ImOkDrawFlags flags,
                            const float* extraColumns = nullptr, int extraColumnCount = 0)
        {
            const float width{ pMax.x - pMin.x };
            const float height{ pMax.y - pMin.y };
            if (width <= 0.0f || height <= 0.0f)
            {
                return;
            }

            const int uniformCellsX{ static_cast<int>(std::fmin(std::ceil(width / SURFACE_CELL_SIZE),
                                                                static_cast<float>(SURFACE_MAX_CELLS - extraColumnCount))) };
            const int cellsY{ static_cast<int>(std::fmin(std::ceil(height / SURFACE_CELL_SIZE),
                                                         static_cast<float>(SURFACE_MAX_CELLS))) };

            float columnU[SURFACE_MAX_CELLS + 1]{};
            int columns{ 0 };
            int extra{ 0 };
            for (int x{ 0 }; x <= uniformCellsX; ++x)
            {
                const float gridU{ static_cast<float>(x) / static_cast<float>(uniformCellsX) };
                while (extra < extraColumnCount && extraColumns[extra] < gridU)
                {
                    columnU[columns++] = extraColumns[extra++];
                }
                columnU[columns++] = gridU;
            }
            const int cellsX{ columns - 1 };
            const ImVec2 uv{ ImGui::GetFontTexUvWhitePixel() };

            drawList->PrimReserve(cellsX * cellsY * 6, columns * (cellsY + 1));
            const ImDrawIdx base{ static_cast<ImDrawIdx>(drawList->_VtxCurrentIdx) };

            for (int y{ 0 }; y <= cellsY; ++y)
            {
                const float v{ static_cast<float>(y) / static_cast<float>(cellsY) };
                for (int x{ 0 }; x <= cellsX; ++x)
                {
                    const float u{ columnU[x] };
                    const ImU32 color{ LinearSrgbToImU32(colorAt(u, v, userData), StyleAlpha(), flags) };
                    drawList->PrimWriteVtx(ImVec2(pMin.x + width * u, pMin.y + height * v), uv, color);
                }
            }

            for (int y{ 0 }; y < cellsY; ++y)
            {
                for (int x{ 0 }; x < cellsX; ++x)
                {
                    const ImDrawIdx topLeft{ static_cast<ImDrawIdx>(base + y * columns + x) };
                    const ImDrawIdx topRight{ static_cast<ImDrawIdx>(topLeft + 1) };
                    const ImDrawIdx bottomLeft{ static_cast<ImDrawIdx>(topLeft + columns) };
                    const ImDrawIdx bottomRight{ static_cast<ImDrawIdx>(bottomLeft + 1) };

                    drawList->PrimWriteIdx(topLeft);
                    drawList->PrimWriteIdx(topRight);
                    drawList->PrimWriteIdx(bottomRight);
                    drawList->PrimWriteIdx(topLeft);
                    drawList->PrimWriteIdx(bottomRight);
                    drawList->PrimWriteIdx(bottomLeft);
                }
            }
        }

        struct LinearSrgbGradient
        {
            LinearSrgb left{};
            LinearSrgb right{};
        };

        LinearSrgb LinearSrgbGradientColorAt(float t, const void* userData)
        {
            const LinearSrgbGradient& g{ *static_cast<const LinearSrgbGradient*>(userData) };
            return LerpLinearSrgb(g.left, g.right, t);
        }

        struct OkLabGradient
        {
            OkLab left{};
            OkLab right{};
        };

        LinearSrgb OkLabGradientColorAt(float t, const void* userData)
        {
            const OkLabGradient& g{ *static_cast<const OkLabGradient*>(userData) };
            return OkLabToLinearSrgb(LerpOkLab(g.left, g.right, t));
        }

        struct OkLChGradient
        {
            OkLCh left{};
            OkLCh right{};
            HueDirection direction{ HueDirection::Shorter };
        };

        LinearSrgb OkLChGradientColorAt(float t, const void* userData)
        {
            const OkLChGradient& g{ *static_cast<const OkLChGradient*>(userData) };
            return OkLabToLinearSrgb(OkLChToOkLab(LerpOkLCh(g.left, g.right, t, g.direction)));
        }

        struct OkhsvSaturationValue
        {
            float hue{ 0.0f };
        };

        LinearSrgb OkhsvSaturationValueColorAt(float u, float v, const void* userData)
        {
            const OkhsvSaturationValue& surface{ *static_cast<const OkhsvSaturationValue*>(userData) };
            return OkLabToLinearSrgb(OkhsvToOkLab({ surface.hue, u, 1.0f - v }));
        }

        struct OkhslSaturationLightness
        {
            float hue{ 0.0f };
        };

        LinearSrgb OkhslSaturationLightnessColorAt(float u, float v, const void* userData)
        {
            const OkhslSaturationLightness& surface{ *static_cast<const OkhslSaturationLightness*>(userData) };
            return OkLabToLinearSrgb(OkhslToOkLab({ surface.hue, u, 1.0f - v }));
        }

        LinearSrgb OkhsvHueSaturationColorAt(float u, float v, const void* /*userData*/)
        {
            return OkLabToLinearSrgb(OkhsvToOkLab({ u * 360.0f, 1.0f - v, 1.0f }));
        }

        LinearSrgb HueBarColorAt(float t, const void* /*userData*/)
        {
            return Internal::HueBarColor(t * 360.0f);
        }

        // Call right after the bar's InvisibleButton
        void ReferenceTooltip(const char* description, const Internal::LightReference* references, int count,
                              const char* note = nullptr)
        {
            if ((count > 0 || note != nullptr) && ImGui::IsItemHovered() && !ImGui::IsItemActive() && ImGui::BeginTooltip())
            {
                ImGui::TextUnformatted(description);
                if (note != nullptr)
                {
                    ImGui::TextUnformatted(note);
                }
                if (count > 0)
                {
                    ImGui::Separator();
                    for (int i{ 0 }; i < count; ++i)
                    {
                        ImGui::TextUnformatted(references[i].text);
                    }
                }
                ImGui::EndTooltip();
            }
        }

        // ImGui's own checkerboard is internal. Encoded bytes, drawn as they are, as ImGui's.
        constexpr ImU32 CHECKERBOARD_DARK{ IM_COL32(128, 128, 128, 255) };
        constexpr ImU32 CHECKERBOARD_LIGHT{ IM_COL32(204, 204, 204, 255) };

        void AddCheckerboard(ImDrawList* drawList, const ImVec2& pMin, const ImVec2& pMax, float cellSize)
        {
            if (cellSize <= 0.0f || pMax.x <= pMin.x || pMax.y <= pMin.y)
            {
                return;
            }

            drawList->AddRectFilled(pMin, pMax, ImGui::GetColorU32(CHECKERBOARD_DARK));

            const int rows{ static_cast<int>(std::ceil((pMax.y - pMin.y) / cellSize)) };
            const int columns{ static_cast<int>(std::ceil((pMax.x - pMin.x) / cellSize)) };
            for (int row{ 0 }; row < rows; ++row)
            {
                for (int column{ (row % 2 == 0) ? 1 : 0 }; column < columns; column += 2)
                {
                    const ImVec2 cellMin{ pMin.x + static_cast<float>(column) * cellSize,
                                          pMin.y + static_cast<float>(row) * cellSize };
                    const ImVec2 cellMax{ std::fmin(cellMin.x + cellSize, pMax.x),
                                          std::fmin(cellMin.y + cellSize, pMax.y) };
                    drawList->AddRectFilled(cellMin, cellMax, ImGui::GetColorU32(CHECKERBOARD_LIGHT));
                }
            }
        }

        typedef void (*DrawSquareFn)(ImDrawList* drawList, const ImVec2& pMin, const ImVec2& pMax,
                                     float hue, ImOkDrawFlags flags);
        typedef LinearSrgb (*ValueColorFn)(float hue, float x, float y);

        struct HueSquareLayout
        {
            DrawSquareFn drawSquare{ nullptr };
            ValueColorFn colorOf{ nullptr };
        };

        LinearSrgb OkhsvColor(float hue, float s, float v)
        {
            return OkLabToLinearSrgb(OkhsvToOkLab({ hue, s, v }));
        }

        LinearSrgb OkhslColor(float hue, float s, float l)
        {
            return OkLabToLinearSrgb(OkhslToOkLab({ hue, s, l }));
        }

        constexpr HueSquareLayout OKHSV_SATURATION_VALUE{ AddRectOkhsvSaturationValue, OkhsvColor };
        constexpr HueSquareLayout OKHSL_SATURATION_LIGHTNESS{ AddRectOkhslSaturationLightness, OkhslColor };

        void AddBarMarker(ImDrawList* drawList, const ImVec2& barMin, const ImVec2& barMax, float y)
        {
            drawList->AddLine(ImVec2(barMin.x - 2.0f, y), ImVec2(barMax.x + 2.0f, y), ImGui::GetColorU32(IM_COL32_BLACK), 4.0f);
            drawList->AddLine(ImVec2(barMin.x - 2.0f, y), ImVec2(barMax.x + 2.0f, y), ImGui::GetColorU32(IM_COL32_WHITE), 2.0f);
        }

        // A press jumps to the point. With Alt or Shift held the value moves with the mouse instead,
        // and stays relative for the rest of the drag, so releasing the key never jumps.
        constexpr float DRAG_SLOW_FACTOR{ 0.01f };
        constexpr float DRAG_FAST_FACTOR{ 10.0f };

        // Call right after the InvisibleButton, while it is active
        bool IsDragRelative(bool speedTweaks)
        {
            const ImGuiIO& io{ ImGui::GetIO() };
            ImGuiStorage* stateStorage{ ImGui::GetStateStorage() };
            const ImGuiID key{ ImGui::GetItemID() };
            const bool relative{ speedTweaks && (io.KeyAlt || io.KeyShift
                                 || (!ImGui::IsItemActivated() && stateStorage->GetBool(key, false))) };
            stateStorage->SetBool(key, relative);
            return relative;
        }

        float DragPosition(float current, bool relative, float mouse, float mouseDelta, float start, float length)
        {
            if (!relative)
            {
                return Clamp01((mouse - start) / length);
            }
            const ImGuiIO& io{ ImGui::GetIO() };
            const float speed{ (io.KeyAlt ? DRAG_SLOW_FACTOR : 1.0f) * (io.KeyShift ? DRAG_FAST_FACTOR : 1.0f) };
            return Clamp01(current + mouseDelta / length * speed);
        }

        void HueSquarePicker(const HueSquareLayout& layout, float& hue, float& x, float& y, float* alpha = nullptr,
                             bool speedTweaks = true)
        {
            ImDrawList* drawList{ ImGui::GetWindowDrawList() };
            const ImGuiStyle& style{ ImGui::GetStyle() };
            const float spacing{ style.ItemInnerSpacing.x };

            const float barWidth{ ImGui::GetFrameHeight() };
            const float barCount{ (alpha != nullptr) ? 2.0f : 1.0f };
            const float squareSize{ std::fmax(barWidth, ImGui::CalcItemWidth() - barCount * (barWidth + spacing)) };

            const ImVec2 squareMin{ ImGui::GetCursorScreenPos() };
            const ImVec2 squareMax{ squareMin.x + squareSize, squareMin.y + squareSize };
            ImGui::InvisibleButton("square", ImVec2(squareSize, squareSize));
            if (ImGui::IsItemActive())
            {
                const ImGuiIO& io{ ImGui::GetIO() };
                const bool relative{ IsDragRelative(speedTweaks) };
                x = DragPosition(x, relative, io.MousePos.x, io.MouseDelta.x, squareMin.x, squareSize);
                y = 1.0f - DragPosition(1.0f - y, relative, io.MousePos.y, io.MouseDelta.y, squareMin.y, squareSize);
            }

            ImGui::SameLine(0.0f, spacing);
            const ImVec2 hueMin{ ImGui::GetCursorScreenPos() };
            const ImVec2 hueMax{ hueMin.x + barWidth, hueMin.y + squareSize };
            ImGui::InvisibleButton("h", ImVec2(barWidth, squareSize));
            if (ImGui::IsItemActive())
            {
                const ImGuiIO& io{ ImGui::GetIO() };
                const float t{ DragPosition(hue / 360.0f, IsDragRelative(speedTweaks), io.MousePos.y, io.MouseDelta.y, hueMin.y, squareSize) };
                hue = std::fmin(t * 360.0f, MAX_HUE);
            }

            ImVec2 alphaMin{};
            ImVec2 alphaMax{};
            if (alpha != nullptr)
            {
                ImGui::SameLine(0.0f, spacing);
                alphaMin = ImGui::GetCursorScreenPos();
                alphaMax = ImVec2(alphaMin.x + barWidth, alphaMin.y + squareSize);
                ImGui::InvisibleButton("a", ImVec2(barWidth, squareSize));
                if (ImGui::IsItemActive())
                {
                    const ImGuiIO& io{ ImGui::GetIO() };
                    *alpha = 1.0f - DragPosition(1.0f - *alpha, IsDragRelative(speedTweaks), io.MousePos.y, io.MouseDelta.y, alphaMin.y, squareSize);
                }
            }

            const LinearSrgb picked{ layout.colorOf(hue, x, y) };
            layout.drawSquare(drawList, squareMin, squareMax, hue, 0);
            AddRectHueBar(drawList, hueMin, hueMax);

            const float markerRadius{ barWidth * 0.25f };
            const ImVec2 marker{ squareMin.x + x * squareSize, squareMin.y + (1.0f - y) * squareSize };
            drawList->AddCircleFilled(marker, markerRadius, LinearSrgbToImU32(picked, StyleAlpha()));
            drawList->AddCircle(marker, markerRadius + 1.0f, ImGui::GetColorU32(IM_COL32_BLACK));
            drawList->AddCircle(marker, markerRadius, ImGui::GetColorU32(IM_COL32_WHITE));

            AddBarMarker(drawList, hueMin, hueMax, hueMin.y + (hue / 360.0f) * squareSize);

            if (alpha != nullptr)
            {
                AddRectAlphaBar(drawList, alphaMin, alphaMax, picked);
                AddBarMarker(drawList, alphaMin, alphaMax, alphaMin.y + (1.0f - Clamp01(*alpha)) * squareSize);
            }
        }

        // NoBrightness's picker: the rect takes the hue bar's place, keeping HueSquarePicker's size
        void HueSaturationPicker(float& hue, float& s, float* alpha = nullptr, bool speedTweaks = true)
        {
            ImDrawList* drawList{ ImGui::GetWindowDrawList() };
            const float spacing{ ImGui::GetStyle().ItemInnerSpacing.x };
            const float barWidth{ ImGui::GetFrameHeight() };
            const float barCount{ (alpha != nullptr) ? 2.0f : 1.0f };
            const float height{ std::fmax(barWidth, ImGui::CalcItemWidth() - barCount * (barWidth + spacing)) };
            const float width{ height + barWidth + spacing };

            const ImVec2 rectMin{ ImGui::GetCursorScreenPos() };
            const ImVec2 rectMax{ rectMin.x + width, rectMin.y + height };
            ImGui::InvisibleButton("square", ImVec2(width, height));
            if (ImGui::IsItemActive())
            {
                const ImGuiIO& io{ ImGui::GetIO() };
                const bool relative{ IsDragRelative(speedTweaks) };
                hue = std::fmin(DragPosition(hue / 360.0f, relative, io.MousePos.x, io.MouseDelta.x, rectMin.x, width) * 360.0f, MAX_HUE);
                s = 1.0f - DragPosition(1.0f - s, relative, io.MousePos.y, io.MouseDelta.y, rectMin.y, height);
            }

            ImVec2 alphaMin{};
            ImVec2 alphaMax{};
            if (alpha != nullptr)
            {
                ImGui::SameLine(0.0f, spacing);
                alphaMin = ImGui::GetCursorScreenPos();
                alphaMax = ImVec2(alphaMin.x + barWidth, alphaMin.y + height);
                ImGui::InvisibleButton("a", ImVec2(barWidth, height));
                if (ImGui::IsItemActive())
                {
                    const ImGuiIO& io{ ImGui::GetIO() };
                    *alpha = 1.0f - DragPosition(1.0f - *alpha, IsDragRelative(speedTweaks), io.MousePos.y, io.MouseDelta.y, alphaMin.y, height);
                }
            }

            const LinearSrgb picked{ OkhsvColor(hue, s, 1.0f) };
            AddRectOkhsvHueSaturation(drawList, rectMin, rectMax);

            const float markerRadius{ barWidth * 0.25f };
            const ImVec2 marker{ rectMin.x + (hue / 360.0f) * width, rectMin.y + (1.0f - s) * height };
            drawList->AddCircleFilled(marker, markerRadius, LinearSrgbToImU32(picked, StyleAlpha()));
            drawList->AddCircle(marker, markerRadius + 1.0f, ImGui::GetColorU32(IM_COL32_BLACK));
            drawList->AddCircle(marker, markerRadius, ImGui::GetColorU32(IM_COL32_WHITE));

            if (alpha != nullptr)
            {
                AddRectAlphaBar(drawList, alphaMin, alphaMax, picked);
                AddBarMarker(drawList, alphaMin, alphaMax, alphaMin.y + (1.0f - Clamp01(*alpha)) * height);
            }
        }

        using Internal::Surface;
        using Internal::SurfaceValue;
        using Internal::SurfaceValueToOkLab;
        using Internal::OkLabToSurfaceValue;
        using Internal::OkLabToSurfaceValueKeeping;
        using Internal::ConvertSurfaceValue;

        const char* SurfaceName(Surface surface)
        {
            switch (surface)
            {
            case Surface::OkhsvSaturationValue:     return "Okhsv (saturation, value)";
            case Surface::OkhslSaturationLightness: return "Okhsl (saturation, lightness)";
            case Surface::Count: break;
            }
            return "";
        }

        const HueSquareLayout& SurfaceLayout(Surface surface)
        {
            return (surface == Surface::OkhslSaturationLightness) ? OKHSL_SATURATION_LIGHTNESS : OKHSV_SATURATION_VALUE;
        }

        Surface LoadSurface(ImOkColorEditFlags flags)
        {
            IM_ASSERT(HasAtMostOneBit(flags & ImOkColorEditFlags_PickerMask_) && "Set at most one ImOkColorEditFlags_Picker* flag.");
            if (flags & ImOkColorEditFlags_NoBrightness)
            {
                IM_ASSERT((flags & ImOkColorEditFlags_PickerOkhsl) == 0 && "ImOkColorEditFlags_NoBrightness picks on Okhsv: Okhsl has no slice where the largest channel is 1.");
                return Surface::OkhsvSaturationValue;
            }
            const int saved{ ImGui::GetStateStorage()->GetInt(ImGui::GetID("surface"), -1) };
            if (saved >= 0 && saved < static_cast<int>(Surface::Count))
            {
                return static_cast<Surface>(saved);
            }
            return (flags & ImOkColorEditFlags_PickerOkhsl) ? Surface::OkhslSaturationLightness
                                                            : Surface::OkhsvSaturationValue;
        }

        // Alpha is edited in place and not reported
        bool PickSurfaceValue(Surface surface, SurfaceValue& value, float* alpha = nullptr, bool noBrightness = false,
                              bool speedTweaks = true)
        {
            const SurfaceValue before{ value };
            if (noBrightness)
            {
                HueSaturationPicker(value.h, value.x, alpha, speedTweaks);
            }
            else
            {
                HueSquarePicker(SurfaceLayout(surface), value.h, value.x, value.y, alpha, speedTweaks);
            }
            return value.h != before.h || value.x != before.x || value.y != before.y;
        }

        // If col is still what was last written, continue from the saved value: it keeps hue at
        // grays and avoids round-trip drift. Otherwise col changed from outside, maybe to another
        // object sharing the ID, so derive everything anew. Alpha is left out: it derives nothing,
        // and comparing it would reset hue at grays whenever code fades alpha.
        SurfaceValue LoadSurfaceState(const float col[3], ImOkStoredAs storage, Surface surface)
        {
            ImGuiStorage* stateStorage{ ImGui::GetStateStorage() };
            const bool unchangedSinceLastWrite{ stateStorage->GetBool(ImGui::GetID("state"), false)
                && col[0] == stateStorage->GetFloat(ImGui::GetID("written.0"))
                && col[1] == stateStorage->GetFloat(ImGui::GetID("written.1"))
                && col[2] == stateStorage->GetFloat(ImGui::GetID("written.2")) };

            if (unchangedSinceLastWrite)
            {
                const SurfaceValue saved{ stateStorage->GetFloat(ImGui::GetID("value.h")),
                                           stateStorage->GetFloat(ImGui::GetID("value.x")),
                                           stateStorage->GetFloat(ImGui::GetID("value.y")) };
                const int savedSurface{ stateStorage->GetInt(ImGui::GetID("value.surface"), 0) };
                if (savedSurface >= 0 && savedSurface < static_cast<int>(Surface::Count))
                {
                    return ConvertSurfaceValue(static_cast<Surface>(savedSurface), surface, saved);
                }
            }
            return OkLabToSurfaceValue(surface, LinearSrgbToOkLab(StoredToLinearSrgb(col, storage)));
        }

        void SaveSurfaceState(Surface surface, const SurfaceValue& value, const float col[3])
        {
            ImGuiStorage* stateStorage{ ImGui::GetStateStorage() };
            stateStorage->SetBool(ImGui::GetID("state"), true);
            stateStorage->SetInt(ImGui::GetID("value.surface"), static_cast<int>(surface));
            stateStorage->SetFloat(ImGui::GetID("value.h"), value.h);
            stateStorage->SetFloat(ImGui::GetID("value.x"), value.x);
            stateStorage->SetFloat(ImGui::GetID("value.y"), value.y);
            stateStorage->SetFloat(ImGui::GetID("written.0"), col[0]);
            stateStorage->SetFloat(ImGui::GetID("written.1"), col[1]);
            stateStorage->SetFloat(ImGui::GetID("written.2"), col[2]);
        }

        // "Color##id" -> "Color". ImGui's FindRenderedTextEnd is internal.
        const char* FindLabelEnd(const char* label)
        {
            const char* hidden{ std::strstr(label, "##") };
            return hidden ? hidden : label + std::strlen(label);
        }

        enum class DisplayMode : int
        {
            Okhsv,
            Okhsl,
            Rgb,
            Hex,
            OkLCh,
            Count,
        };

        const char* DisplayModeName(DisplayMode mode, ImOkStoredAs storage)
        {
            switch (mode)
            {
            case DisplayMode::Okhsv: return "Okhsv";
            case DisplayMode::Okhsl: return "Okhsl";
            case DisplayMode::Rgb:   return (storage == ImOkStoredAs_EncodedSrgb) ? "RGB (stored: encoded sRGB)" : "RGB (stored: linear sRGB)";
            case DisplayMode::Hex:   return "Hex (encoded sRGB)";
            case DisplayMode::OkLCh: return "OkLCh (read-only)";
            case DisplayMode::Count: break;
            }
            return "";
        }

        DisplayMode SurfaceDisplayMode(Surface surface)
        {
            return (surface == Surface::OkhslSaturationLightness) ? DisplayMode::Okhsl : DisplayMode::Okhsv;
        }

        bool IsSurfaceDisplayMode(DisplayMode mode)
        {
            return mode == DisplayMode::Okhsv || mode == DisplayMode::Okhsl;
        }

        DisplayMode LoadDisplayMode(ImOkColorEditFlags flags, Surface surface)
        {
            IM_ASSERT(HasAtMostOneBit(flags & ImOkColorEditFlags_DisplayMask_) && "Set at most one ImOkColorEditFlags_Display* flag.");
            const bool noBrightness{ (flags & ImOkColorEditFlags_NoBrightness) != 0 };
            IM_ASSERT(!(noBrightness && (flags & ImOkColorEditFlags_DisplayOkhsl)) && "ImOkColorEditFlags_NoBrightness has no Okhsl fields: Okhsl's l would edit brightness.");
            const int saved{ ImGui::GetStateStorage()->GetInt(ImGui::GetID("display"), -1) };
            if (saved >= 0 && saved < static_cast<int>(DisplayMode::Count))
            {
                const DisplayMode savedMode{ static_cast<DisplayMode>(saved) };
                return (noBrightness && savedMode == DisplayMode::Okhsl) ? DisplayMode::Okhsv : savedMode;
            }
            if (flags & ImOkColorEditFlags_DisplayOkhsv) { return DisplayMode::Okhsv; }
            if (flags & ImOkColorEditFlags_DisplayOkhsl) { return DisplayMode::Okhsl; }
            if (flags & ImOkColorEditFlags_DisplayRgb)   { return DisplayMode::Rgb; }
            if (flags & ImOkColorEditFlags_DisplayHex)   { return DisplayMode::Hex; }
            if (flags & ImOkColorEditFlags_DisplayOkLCh) { return DisplayMode::OkLCh; }
            return SurfaceDisplayMode(surface);
        }

        enum class EditKind
        {
            None,
            Value,  // The surface value; col follows
            Stored, // col; the surface value follows, and col stays as typed
        };

        EditKind Merge(EditKind current, EditKind next)
        {
            return (next != EditKind::None) ? next : current;
        }

        // Call right after a ColorButton with ImGuiColorEditFlags_NoDragDrop: its own source sends
        // the 8-bit display color. This sends col exactly.
        void ColorDragSource(const float col[3], const float* alpha, ImOkStoredAs storage,
                             const ImVec4& shown, ImGuiColorEditFlags previewFlags, ImOkColorEditFlags flags)
        {
            if ((flags & ImOkColorEditFlags_NoDragDrop) != 0 || !ImGui::BeginDragDropSource())
            {
                return;
            }

            const EncodedSrgb encoded{ Internal::StoredToEncodedSrgb(col, storage) };
            if (alpha != nullptr)
            {
                const float payload[4]{ encoded.r, encoded.g, encoded.b, *alpha };
                ImGui::SetDragDropPayload(IMGUI_PAYLOAD_TYPE_COLOR_4F, payload, sizeof(payload), ImGuiCond_Once);
            }
            else
            {
                const float payload[3]{ encoded.r, encoded.g, encoded.b };
                ImGui::SetDragDropPayload(IMGUI_PAYLOAD_TYPE_COLOR_3F, payload, sizeof(payload), ImGuiCond_Once);
            }

            ImGui::ColorButton("##preview", shown, previewFlags | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop);
            ImGui::SameLine();
            ImGui::TextUnformatted("Color");
            ImGui::EndDragDropSource();
        }

        // Call after the swatch's ColorButton and its drag source
        void SwatchTooltip(const ImVec4& shown, ImGuiColorEditFlags previewFlags, const char* const lines[], int lineCount)
        {
            if (!ImGui::BeginItemTooltip())
            {
                return;
            }
            const float size{ ImGui::GetFontSize() * 3.0f + ImGui::GetStyle().FramePadding.y * 2.0f };
            ImGui::ColorButton("##preview", shown, previewFlags | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                               ImVec2(size, size));
            ImGui::SameLine();
            ImGui::BeginGroup();
            for (int i{ 0 }; i < lineCount; ++i)
            {
                ImGui::TextUnformatted(lines[i]);
            }
            ImGui::EndGroup();
            ImGui::EndTooltip();
        }

        void ColorSwatchTooltip(const float col[3], const float* alpha, ImOkStoredAs storage, const ImVec4& shown,
                                ImGuiColorEditFlags previewFlags, ImOkColorEditFlags flags)
        {
            if ((flags & ImOkColorEditFlags_NoTooltip) != 0)
            {
                return;
            }
            const Internal::ColorTooltipText text{ Internal::ColorTooltipLines(col, alpha, storage) };
            const char* const lines[]{ text.stored, text.hex, text.oklch };
            SwatchTooltip(shown, previewFlags, lines, IM_ARRAYSIZE(lines));
        }

        void LightSwatchTooltip(const LinearSrgb& color, const float* kelvin, float intensity, ImOkLightUnit unit,
                                const ImVec4& shown, ImGuiColorEditFlags previewFlags, ImOkLightEditFlags flags)
        {
            if ((flags & ImOkLightEditFlags_NoTooltip) != 0)
            {
                return;
            }
            const Internal::LightTooltipText text{ Internal::LightTooltipLines(color, kelvin, intensity, unit) };
            const char* const lines[]{ text.shows, text.passes };
            SwatchTooltip(shown, previewFlags, lines, IM_ARRAYSIZE(lines));
        }

        // Returns the number of floats accepted (3 or 4), or 0
        int AcceptColorDrop(float data[4], ImOkColorEditFlags flags)
        {
            if ((flags & ImOkColorEditFlags_NoDragDrop) != 0
                || (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0 // Unlike ImGui: a drop is an interaction
                || !ImGui::BeginDragDropTarget())
            {
                return 0;
            }

            int count{ 0 };
            if (const ImGuiPayload* payload{ ImGui::AcceptDragDropPayload(IMGUI_PAYLOAD_TYPE_COLOR_3F) })
            {
                if (payload->DataSize >= static_cast<int>(sizeof(float) * 3))
                {
                    std::memcpy(data, payload->Data, sizeof(float) * 3);
                    count = 3;
                }
            }
            if (const ImGuiPayload* payload{ ImGui::AcceptDragDropPayload(IMGUI_PAYLOAD_TYPE_COLOR_4F) })
            {
                if (payload->DataSize >= static_cast<int>(sizeof(float) * 4))
                {
                    std::memcpy(data, payload->Data, sizeof(float) * 4);
                    count = 4;
                }
            }
            ImGui::EndDragDropTarget();
            return count;
        }

        // The pickers' InvisibleButtons never mark themselves. Call after EndGroup.
        void MarkEdited(bool changed)
        {
            const ImGuiID id{ ImGui::GetItemID() };
            if (changed && id != 0)
            {
                ImGui::MarkItemEdited(id);
            }
        }

        float FieldWidth(float totalWidth, int count, int index)
        {
            const float spacing{ ImGui::GetStyle().ItemInnerSpacing.x };
            const float items{ totalWidth - spacing * static_cast<float>(count - 1) };
            const float start{ std::floor(items * static_cast<float>(index) / static_cast<float>(count)) };
            const float end{ (index == count - 1)
                ? items
                : std::floor(items * static_cast<float>(index + 1) / static_cast<float>(count)) };
            return std::fmax(end - start, 1.0f);
        }

        void AlphaField(float* alpha, bool percent, float width)
        {
            ImGui::SetNextItemWidth(width);
            if (!percent)
            {
                ImGui::DragFloat("##a", alpha, 0.002f, 0.0f, 1.0f, "A:%.3f", ImGuiSliderFlags_AlwaysClamp);
                return;
            }

            // Written only on an edit: * 100 / 100 can nudge the value
            float alphaPercent{ *alpha * 100.0f };
            if (ImGui::DragFloat("##a", &alphaPercent, 0.5f, 0.0f, 100.0f, "A:%.0f%%", ImGuiSliderFlags_AlwaysClamp))
            {
                *alpha = alphaPercent / 100.0f;
            }
        }

        // yFormat nullptr: no y field (NoBrightness)
        bool HueFields(const char* id, SurfaceValue& value, const char* yFormat, float totalWidth, float* alpha = nullptr)
        {
            const float spacing{ ImGui::GetStyle().ItemInnerSpacing.x };
            const int valueCount{ (yFormat != nullptr) ? 3 : 2 };
            const int count{ (alpha != nullptr) ? valueCount + 1 : valueCount };
            bool changed{ false };

            ImGui::PushID(id);

            ImGui::SetNextItemWidth(FieldWidth(totalWidth, count, 0));
            changed |= ImGui::DragFloat("##h", &value.h, 0.5f, 0.0f, 360.0f, "H:%.0f", ImGuiSliderFlags_AlwaysClamp);

            ImGui::SameLine(0.0f, spacing);
            float xPercent{ value.x * 100.0f };
            ImGui::SetNextItemWidth(FieldWidth(totalWidth, count, 1));
            if (ImGui::DragFloat("##x", &xPercent, 0.5f, 0.0f, 100.0f, "S:%.0f%%", ImGuiSliderFlags_AlwaysClamp))
            {
                value.x = xPercent / 100.0f;
                changed = true;
            }

            if (yFormat != nullptr)
            {
                ImGui::SameLine(0.0f, spacing);
                float yPercent{ value.y * 100.0f };
                ImGui::SetNextItemWidth(FieldWidth(totalWidth, count, 2));
                if (ImGui::DragFloat("##y", &yPercent, 0.5f, 0.0f, 100.0f, yFormat, ImGuiSliderFlags_AlwaysClamp))
                {
                    value.y = yPercent / 100.0f;
                    changed = true;
                }
            }

            if (alpha != nullptr)
            {
                ImGui::SameLine(0.0f, spacing);
                AlphaField(alpha, true, FieldWidth(totalWidth, count, valueCount));
            }

            ImGui::PopID();
            return changed;
        }

        // readOnly: NoBrightness, whose color has no free third value
        bool RgbFields(float col[3], float totalWidth, float* alpha = nullptr, bool readOnly = false)
        {
            const char* const formats[]{ "R:%.3f", "G:%.3f", "B:%.3f" };
            const float spacing{ ImGui::GetStyle().ItemInnerSpacing.x };
            const int count{ (alpha != nullptr) ? 4 : 3 };
            bool changed{ false };

            ImGui::PushID("rgb");
            ImGui::BeginDisabled(readOnly);
            for (int i{ 0 }; i < 3; ++i)
            {
                if (i > 0)
                {
                    ImGui::SameLine(0.0f, spacing);
                }
                ImGui::PushID(i);
                ImGui::SetNextItemWidth(FieldWidth(totalWidth, count, i));
                changed |= ImGui::DragFloat("##c", &col[i], 0.002f, 0.0f, 1.0f, formats[i], ImGuiSliderFlags_AlwaysClamp);
                ImGui::PopID();
            }
            ImGui::EndDisabled();
            if (alpha != nullptr)
            {
                ImGui::SameLine(0.0f, spacing);
                AlphaField(alpha, false, FieldWidth(totalWidth, count, 3));
            }
            ImGui::PopID();
            return changed;
        }

        // Written only where the bytes changed, so editing AA leaves col exact
        bool HexField(float col[3], ImOkStoredAs storage, float width, float* alpha = nullptr)
        {
            const EncodedSrgb shown{ Internal::StoredToEncodedSrgb(col, storage) };

            // Room past #RRGGBBAA: an extra keystroke is rejected, not cut off
            char buffer[16]{};
            EncodedSrgbToHex(shown, buffer, alpha);

            ImGui::SetNextItemWidth(width);
            if (!ImGui::InputText("##hex", buffer, sizeof(buffer), ImGuiInputTextFlags_CharsUppercase))
            {
                return false;
            }

            EncodedSrgb typed{};
            float typedAlpha{ 1.0f };
            if (!Internal::HexToEncodedSrgb(buffer, &typed, (alpha != nullptr) ? &typedAlpha : nullptr,
                                            Internal::HexForms::LongOnly))
            {
                return false;
            }

            if (alpha != nullptr && ChannelToByte(typedAlpha) != ChannelToByte(*alpha))
            {
                *alpha = typedAlpha;
            }
            if (ChannelToByte(typed.r) == ChannelToByte(shown.r)
                && ChannelToByte(typed.g) == ChannelToByte(shown.g)
                && ChannelToByte(typed.b) == ChannelToByte(shown.b))
            {
                return false;
            }

            Internal::EncodedSrgbToStored(typed, storage, col);
            return true;
        }

        // Read-only InputText, not Text, so it can be selected and copied
        void OkLChText(const float col[3], ImOkStoredAs storage, float width, const float* alpha = nullptr)
        {
            const OkLCh lch{ OkLabToOkLCh(LinearSrgbToOkLab(StoredToLinearSrgb(col, storage))) };

            char buffer[Internal::COPY_BUFFER_SIZE]{};
            Internal::OkLChToCssText(lch, alpha, buffer);

            ImGui::SetNextItemWidth(width);
            ImGui::InputText("##oklch", buffer, sizeof(buffer), ImGuiInputTextFlags_ReadOnly);
        }

        EditKind PerceptualFields(Surface fieldSpace, const char* id, const char* yFormat,
                                  Surface surface, SurfaceValue& value, float width, float* alpha = nullptr)
        {
            SurfaceValue fieldValue{ ConvertSurfaceValue(surface, fieldSpace, value) };
            if (!HueFields(id, fieldValue, yFormat, width, alpha))
            {
                return EditKind::None;
            }
            value = ConvertSurfaceValue(fieldSpace, surface, fieldValue);
            return EditKind::Value;
        }

        EditKind DisplayFields(DisplayMode mode, Surface surface, SurfaceValue& value, float col[3],
                               ImOkStoredAs storage, float width, float* alpha = nullptr, bool noBrightness = false)
        {
            switch (mode)
            {
            case DisplayMode::Okhsv:
                return PerceptualFields(Surface::OkhsvSaturationValue, "okhsv", noBrightness ? nullptr : "V:%.0f%%",
                                        surface, value, width, alpha);
            case DisplayMode::Okhsl:
                return PerceptualFields(Surface::OkhslSaturationLightness, "okhsl", "L:%.0f%%", surface, value, width, alpha);
            case DisplayMode::Rgb:
                return RgbFields(col, width, alpha, noBrightness) ? EditKind::Stored : EditKind::None;
            case DisplayMode::Hex:
                return HexField(col, storage, width, alpha) ? EditKind::Stored : EditKind::None;
            case DisplayMode::OkLCh:
                OkLChText(col, storage, width, alpha);
                return EditKind::None;
            case DisplayMode::Count:
                break;
            }
            return EditKind::None;
        }

        void DisplayMenuItems(ImGuiStorage* stateStorage, ImGuiID displayId, DisplayMode mode, ImOkStoredAs storage,
                              bool noBrightness = false)
        {
            ImGui::SeparatorText("Display");
            for (int i{ 0 }; i < static_cast<int>(DisplayMode::Count); ++i)
            {
                const DisplayMode option{ static_cast<DisplayMode>(i) };
                if (noBrightness && option == DisplayMode::Okhsl)
                {
                    continue;
                }
                if (ImGui::MenuItem(DisplayModeName(option, storage), nullptr, option == mode))
                {
                    stateStorage->SetInt(displayId, i);
                }
            }
        }

        void PickerMenuItems(ImGuiStorage* stateStorage, ImGuiID surfaceId, Surface surface,
                             const ImGuiID* displayId, DisplayMode mode)
        {
            ImGui::SeparatorText("Picker");
            for (int i{ 0 }; i < static_cast<int>(Surface::Count); ++i)
            {
                const Surface option{ static_cast<Surface>(i) };
                if (ImGui::MenuItem(SurfaceName(option), nullptr, option == surface))
                {
                    stateStorage->SetInt(surfaceId, i);
                    // Fields showing the picker's values follow it, overriding a Display flag
                    if (displayId != nullptr && IsSurfaceDisplayMode(mode))
                    {
                        stateStorage->SetInt(*displayId, static_cast<int>(SurfaceDisplayMode(option)));
                    }
                }
            }
        }

        // Always col exactly, never the shown or raised color
        void CopyMenuItems(const float col[3], const float* alpha, ImOkStoredAs storage, bool separated)
        {
            if (separated)
            {
                ImGui::Separator();
            }
            if (!ImGui::BeginMenu("Copy as"))
            {
                return;
            }
            for (int i{ 0 }; i < static_cast<int>(Internal::CopyFormat::Count); ++i)
            {
                const Internal::CopyFormat format{ static_cast<Internal::CopyFormat>(i) };
                char text[Internal::COPY_BUFFER_SIZE]{};
                if (Internal::CopyText(format, col, alpha, storage, text)
                    && ImGui::MenuItem(Internal::CopyFormatName(format, storage), text))
                {
                    ImGui::SetClipboardText(text);
                }
            }
            ImGui::EndMenu();
        }

        EditKind PickerPopupContent(const char* label, const char* labelEnd, Surface surface, SurfaceValue& value,
                                    float col[3], const float original[3], ImOkStoredAs storage,
                                    ImGuiStorage* stateStorage, ImGuiID surfaceId, ImGuiID displayId, DisplayMode mode,
                                    ImOkColorEditFlags flags, float* alpha = nullptr, float originalAlpha = 1.0f)
        {
            const float spacing{ ImGui::GetStyle().ItemInnerSpacing.x };
            const float frame{ ImGui::GetFrameHeight() };
            const float pickerWidth{ frame * 12.0f };
            const ImVec2 previewSize{ frame * 3.0f, frame * 2.0f };
            const ImGuiColorEditFlags previewFlags{ ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_AlphaPreviewHalf
                                                    | ImGuiColorEditFlags_NoDragDrop };
            const bool noBrightness{ (flags & ImOkColorEditFlags_NoBrightness) != 0 };
            EditKind edit{ EditKind::None };

            if (labelEnd != label)
            {
                ImGui::TextUnformatted(label, labelEnd);
                ImGui::Separator();
            }

            // Grouped so the right-click menu covers the square and both bars
            ImGui::PushItemWidth(pickerWidth);
            ImGui::BeginGroup();
            if (PickSurfaceValue(surface, value, alpha, noBrightness, (flags & ImOkColorEditFlags_NoSpeedTweaks) == 0))
            {
                edit = EditKind::Value;
            }
            ImGui::EndGroup();
            ImGui::PopItemWidth();

            // No Display section: the popup shows every display mode
            if (ImGui::BeginPopupContextItem("picker options"))
            {
                if (!noBrightness)
                {
                    PickerMenuItems(stateStorage, surfaceId, surface, &displayId, mode);
                }
                CopyMenuItems(col, alpha, storage, !noBrightness);
                ImGui::EndPopup();
            }

            ImGui::SameLine(0.0f, spacing);
            ImGui::BeginGroup();
            // Focus starts on the first field, not a swatch, as ImGui's side preview
            ImGui::PushItemFlag(ImGuiItemFlags_NoNavDefaultFocus, true);
            // Current sends col, not the surface value it shows
            ImGui::TextUnformatted("Current");
            const ImVec4 currentColor{ LinearSrgbToImVec4(OkLabToLinearSrgb(SurfaceValueToOkLab(surface, value)),
                                                (alpha != nullptr) ? *alpha : 1.0f) };
            ImGui::ColorButton("##current", currentColor, previewFlags, previewSize);
            ColorDragSource(col, alpha, storage, currentColor, previewFlags, flags);
            ColorSwatchTooltip(col, alpha, storage, currentColor, previewFlags, flags);

            ImGui::TextUnformatted("Original");
            const ImVec4 originalColor{ LinearSrgbToImVec4(StoredToLinearSrgb(original, storage), originalAlpha) };
            const bool restoreOriginal{ ImGui::ColorButton("##original", originalColor, previewFlags, previewSize) };
            ColorDragSource(original, (alpha != nullptr) ? &originalAlpha : nullptr, storage, originalColor, previewFlags, flags);
            ColorSwatchTooltip(original, (alpha != nullptr) ? &originalAlpha : nullptr, storage, originalColor, previewFlags, flags);
            if (restoreOriginal)
            {
                // The exact stored values, not recomputed through the surface
                col[0] = original[0];
                col[1] = original[1];
                col[2] = original[2];
                if (alpha != nullptr)
                {
                    *alpha = originalAlpha;
                }
                edit = EditKind::Stored;
            }
            ImGui::PopItemFlag();
            ImGui::EndGroup();

            ImGui::Separator();
            const float fieldsWidth{ pickerWidth + spacing + previewSize.x };
            for (int i{ 0 }; i < static_cast<int>(DisplayMode::Count); ++i)
            {
                const DisplayMode shown{ static_cast<DisplayMode>(i) };
                if (noBrightness && shown == DisplayMode::Okhsl)
                {
                    continue;
                }
                edit = Merge(edit, DisplayFields(shown, surface, value, col, storage, fieldsWidth, alpha, noBrightness));
            }

            return edit;
        }

        EditKind DropOnStored(float col[3], float* alpha, ImOkStoredAs storage, ImOkColorEditFlags flags)
        {
            float data[4]{};
            const int count{ AcceptColorDrop(data, flags) };
            if (count == 0)
            {
                return EditKind::None;
            }
            const Internal::DropResult result{ Internal::ApplyColorPayload(data, count, storage, col, alpha) };
            return result.rgbChanged ? EditKind::Stored : EditKind::None;
        }

        // A Stored edit keeps what col doesn't define, such as hue at a gray: unlike ImGui, on purpose
        void ApplyEdit(EditKind edit, Surface surface, SurfaceValue& value, float col[3], ImOkStoredAs storage,
                       bool noBrightness = false)
        {
            if (edit == EditKind::Value)
            {
                LinearSrgbToStored(OkLabToLinearSrgb(SurfaceValueToOkLab(surface, value)), storage, col);
                if (noBrightness)
                {
                    Internal::RaiseStoredToLargestChannel(col, storage);
                }
            }
            else if (edit == EditKind::Stored)
            {
                if (noBrightness)
                {
                    Internal::RaiseStoredToLargestChannel(col, storage);
                }
                value = OkLabToSurfaceValueKeeping(surface, LinearSrgbToOkLab(StoredToLinearSrgb(col, storage)), value);
                if (noBrightness)
                {
                    value.y = 1.0f;
                }
            }
        }

        bool ColorPickerImpl(const char* label, float col[3], float* alpha, ImOkStoredAs storage, ImOkColorEditFlags flags)
        {
            ImGui::PushID(label);

            ImGuiStorage* stateStorage{ ImGui::GetStateStorage() };
            const ImGuiID surfaceId{ ImGui::GetID("surface") };

            const bool noBrightness{ (flags & ImOkColorEditFlags_NoBrightness) != 0 };
            const Surface surface{ LoadSurface(flags) };
            SurfaceValue value{ LoadSurfaceState(col, storage, surface) };
            if (noBrightness)
            {
                value.y = 1.0f;
            }
            const float alphaBefore{ (alpha != nullptr) ? *alpha : 0.0f };

            ImGui::BeginGroup();
            EditKind edit{ PickSurfaceValue(surface, value, alpha, noBrightness, (flags & ImOkColorEditFlags_NoSpeedTweaks) == 0)
                           ? EditKind::Value : EditKind::None };
            ImGui::EndGroup();

            // The whole picker is the target: unlike ImGui's, it has no rows to drop on. Before
            // saving, so a drop is an edit, not a change from outside.
            edit = Merge(edit, DropOnStored(col, alpha, storage, flags));

            ApplyEdit(edit, surface, value, col, storage, noBrightness);
            SaveSurfaceState(surface, value, col);
            const bool changed{ edit != EditKind::None || (alpha != nullptr && *alpha != alphaBefore) };
            MarkEdited(changed); // Before the menu, a window of its own

            if ((flags & ImOkColorEditFlags_NoOptions) == 0 && ImGui::BeginPopupContextItem("options"))
            {
                if (!noBrightness)
                {
                    PickerMenuItems(stateStorage, surfaceId, surface, nullptr, DisplayMode::Count);
                }
                CopyMenuItems(col, alpha, storage, !noBrightness);
                ImGui::EndPopup();
            }

            ImGui::PopID();
            return edit != EditKind::None || (alpha != nullptr && *alpha != alphaBefore);
        }

        bool ColorEditImpl(const char* label, float col[3], float* alpha, ImOkStoredAs storage, ImOkColorEditFlags flags)
        {
            ImGui::PushID(label);
            ImGui::BeginGroup();

            const ImGuiStyle& style{ ImGui::GetStyle() };
            const float spacing{ style.ItemInnerSpacing.x };
            const float squareSize{ ImGui::GetFrameHeight() };
            const float inputsWidth{ std::fmax(1.0f, ImGui::CalcItemWidth() - squareSize - spacing) };
            const char* labelEnd{ FindLabelEnd(label) };

            // Here: inside the popups, GetID and GetStateStorage refer to the popup window
            ImGuiStorage* stateStorage{ ImGui::GetStateStorage() };
            const ImGuiID originalIds[4]{ ImGui::GetID("original.0"), ImGui::GetID("original.1"),
                                          ImGui::GetID("original.2"), ImGui::GetID("original.3") };
            const ImGuiID displayId{ ImGui::GetID("display") };
            const ImGuiID surfaceId{ ImGui::GetID("surface") };

            const bool noBrightness{ (flags & ImOkColorEditFlags_NoBrightness) != 0 };
            const Surface surface{ LoadSurface(flags) };
            SurfaceValue value{ LoadSurfaceState(col, storage, surface) };
            if (noBrightness)
            {
                value.y = 1.0f;
            }
            const DisplayMode mode{ LoadDisplayMode(flags, surface) };
            const float alphaBefore{ (alpha != nullptr) ? *alpha : 0.0f };
            EditKind edit{ EditKind::None };

            const bool noInputs{ (flags & ImOkColorEditFlags_NoInputs) != 0 };
            if (!noInputs)
            {
                edit = Merge(edit, DisplayFields(mode, surface, value, col, storage, inputsWidth, alpha, noBrightness));
                ImGui::SameLine(0.0f, spacing);
            }

            const ImVec4 swatchColor{ LinearSrgbToImVec4(OkLabToLinearSrgb(SurfaceValueToOkLab(surface, value)),
                                               (alpha != nullptr) ? *alpha : 1.0f) };
            const ImGuiColorEditFlags swatchFlags{ ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop };
            const bool swatchClicked{ ImGui::ColorButton("##swatch", swatchColor, swatchFlags, ImVec2(squareSize, squareSize)) };
            const ImVec2 popupPos{ ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + style.ItemSpacing.y };
            ColorDragSource(col, alpha, storage, swatchColor, swatchFlags, flags);
            ColorSwatchTooltip(col, alpha, storage, swatchColor, swatchFlags, flags);
            if (swatchClicked)
            {
                for (int i{ 0 }; i < 3; ++i)
                {
                    stateStorage->SetFloat(originalIds[i], col[i]);
                }
                if (alpha != nullptr)
                {
                    stateStorage->SetFloat(originalIds[3], *alpha);
                }
                ImGui::OpenPopup("picker");
            }

            const float original[3]{ stateStorage->GetFloat(originalIds[0]),
                                     stateStorage->GetFloat(originalIds[1]),
                                     stateStorage->GetFloat(originalIds[2]) };
            const float originalAlpha{ (alpha != nullptr) ? stateStorage->GetFloat(originalIds[3], 1.0f) : 1.0f };
            // Here, not at OpenPopup: a tooltip opens a window in between and would take the position
            ImGui::SetNextWindowPos(popupPos, ImGuiCond_Appearing);
            if (ImGui::BeginPopup("picker"))
            {
                edit = Merge(edit, PickerPopupContent(label, labelEnd, surface, value, col, original, storage,
                             stateStorage, surfaceId, displayId, mode, flags, alpha, originalAlpha));
                ImGui::EndPopup();
            }

            if (labelEnd != label)
            {
                ImGui::SameLine(0.0f, spacing);
                ImGui::TextUnformatted(label, labelEnd);
            }

            ImGui::EndGroup();

            edit = Merge(edit, DropOnStored(col, alpha, storage, flags));

            ApplyEdit(edit, surface, value, col, storage, noBrightness);
            SaveSurfaceState(surface, value, col);
            const bool changed{ edit != EditKind::None || (alpha != nullptr && *alpha != alphaBefore) };
            MarkEdited(changed);  // Before the menu, a window of its own

            if ((flags & ImOkColorEditFlags_NoOptions) == 0 && ImGui::BeginPopupContextItem("options"))
            {
                if (!noInputs)
                {
                    DisplayMenuItems(stateStorage, displayId, mode, storage, noBrightness);
                }
                if (!noBrightness)
                {
                    PickerMenuItems(stateStorage, surfaceId, surface, &displayId, mode);
                }
                CopyMenuItems(col, alpha, storage, !(noInputs && noBrightness));
                ImGui::EndPopup();
            }

            ImGui::PopID();
            return changed;
        }

        // Position 0 at the left, or at the bottom when vertical
        struct LightBar
        {
            ImVec2 min{};
            ImVec2 max{};
            bool vertical{ false };

            float Length() const { return vertical ? max.y - min.y : max.x - min.x; }

            float At(float position) const { return vertical ? max.y - position * Length() : min.x + position * Length(); }
        };

        // Vertical: opens a group for the bar and field, closed in LightBarLabel
        LightBar LightBarButton(float fieldWidth, const ImVec2* verticalSize)
        {
            const ImGuiStyle& style{ ImGui::GetStyle() };
            const float frame{ ImGui::GetFrameHeight() };
            LightBar bar{};
            bar.vertical = (verticalSize != nullptr);
            ImVec2 size{};
            if (bar.vertical)
            {
                const float height{ (verticalSize->y > 0.0f) ? verticalSize->y : frame * VERTICAL_LIGHT_DEFAULT_FRAMES };
                size = ImVec2(std::fmax(verticalSize->x, fieldWidth),
                              std::fmax(frame, height - frame - style.ItemSpacing.y));
                ImGui::BeginGroup();
            }
            else
            {
                size = ImVec2(std::fmax(frame, ImGui::CalcItemWidth() - fieldWidth - style.ItemInnerSpacing.x), frame);
            }
            bar.min = ImGui::GetCursorScreenPos();
            bar.max = ImVec2(bar.min.x + size.x, bar.min.y + size.y);
            ImGui::InvisibleButton("bar", size);
            return bar;
        }

        float DragBarPosition(const LightBar& bar, float current, bool speedTweaks)
        {
            const ImGuiIO& io{ ImGui::GetIO() };
            const bool relative{ IsDragRelative(speedTweaks) };
            if (bar.vertical)
            {
                return 1.0f - DragPosition(1.0f - current, relative, io.MousePos.y, io.MouseDelta.y, bar.min.y, bar.Length());
            }
            return DragPosition(current, relative, io.MousePos.x, io.MouseDelta.x, bar.min.x, bar.Length());
        }

        LinearSrgb TemperatureGradientColorAt(float t, const void* userData)
        {
            const bool vertical{ *static_cast<const bool*>(userData) };
            return KelvinToLinearSrgb(Internal::BarPositionToKelvin(vertical ? 1.0f - t : t));
        }

        float IntensityRampLightness(float position)
        {
            return INTENSITY_BAR_DARKEST + (1.0f - INTENSITY_BAR_DARKEST) * position;
        }

        LinearSrgb IntensityGradientColorAt(float t, const void* userData)
        {
            const bool vertical{ *static_cast<const bool*>(userData) };
            return OkLabToLinearSrgb({ IntensityRampLightness(vertical ? 1.0f - t : t), 0.0f, 0.0f });
        }

        void AddReferenceTick(ImDrawList* drawList, const LightBar& bar, float position, float lightness)
        {
            const float length{ ImGui::GetFrameHeight() * 0.4f };
            const ImU32 color{ ImGui::GetColorU32((lightness > 0.6f) ? IM_COL32(0, 0, 0, 160) : IM_COL32(255, 255, 255, 160)) };
            const float at{ bar.At(position) };
            if (bar.vertical)
            {
                drawList->AddLine(ImVec2(bar.max.x - length, at), ImVec2(bar.max.x, at), color, 1.0f);
            }
            else
            {
                drawList->AddLine(ImVec2(at, bar.max.y - length), ImVec2(at, bar.max.y), color, 1.0f);
            }
        }

        void AddLightBarMarker(ImDrawList* drawList, const LightBar& bar, float position)
        {
            const float at{ bar.At(position) };
            const ImVec2 from{ bar.vertical ? ImVec2(bar.min.x - 2.0f, at) : ImVec2(at, bar.min.y - 2.0f) };
            const ImVec2 to{ bar.vertical ? ImVec2(bar.max.x + 2.0f, at) : ImVec2(at, bar.max.y + 2.0f) };
            drawList->AddLine(from, to, ImGui::GetColorU32(IM_COL32_BLACK), 4.0f);
            drawList->AddLine(from, to, ImGui::GetColorU32(IM_COL32_WHITE), 2.0f);
        }

        void LightBarFieldLayout(const LightBar& bar, float fieldWidth)
        {
            if (bar.vertical)
            {
                ImGui::SetNextItemWidth(bar.max.x - bar.min.x);
            }
            else
            {
                ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                ImGui::SetNextItemWidth(fieldWidth);
            }
        }

        void LightBarLabel(const LightBar& bar, const char* label)
        {
            if (bar.vertical)
            {
                ImGui::EndGroup();
            }
            const char* labelEnd{ FindLabelEnd(label) };
            if (labelEnd != label)
            {
                ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                ImGui::TextUnformatted(label, labelEnd);
            }
        }

        bool TemperatureEditImpl(const char* label, float* kelvin, ImOkLightEditFlags flags, const ImVec2* verticalSize)
        {
            const bool speedTweaks{ (flags & ImOkLightEditFlags_NoSpeedTweaks) == 0 };
            const bool showReferences{ (flags & ImOkLightEditFlags_NoReferences) == 0 };
            ImGui::PushID(label);
            const float before{ *kelvin };
            const float fieldWidth{ ImGui::CalcTextSize("00000 K").x + ImGui::GetStyle().FramePadding.x * 2.0f };

            ImGui::BeginGroup();

            const LightBar bar{ LightBarButton(fieldWidth, verticalSize) };
            if (ImGui::IsItemActive())
            {
                *kelvin = Internal::BarPositionToKelvin(DragBarPosition(bar, Internal::KelvinToBarPosition(*kelvin), speedTweaks));
            }
            const Internal::TemperatureReferences references{ showReferences ? Internal::GetTemperatureReferences()
                                                                             : Internal::TemperatureReferences{} };
            ReferenceTooltip("Color temperature: a blackbody's color", references.references, references.count);

            ImDrawList* drawList{ ImGui::GetWindowDrawList() };
            AddRectGradient(drawList, bar.min, bar.max, bar.vertical ? GradientAxis::Vertical : GradientAxis::Horizontal,
                            TemperatureGradientColorAt, &bar.vertical, 0);
            for (int i{ 0 }; i < references.count; ++i)
            {
                const float value{ references.references[i].value };
                if (value >= MIN_KELVIN && value <= MAX_KELVIN)
                {
                    AddReferenceTick(drawList, bar, Internal::KelvinToBarPosition(value),
                                     LinearSrgbToOkLab(KelvinToLinearSrgb(value)).L);
                }
            }
            AddLightBarMarker(drawList, bar, Internal::KelvinToBarPosition(*kelvin));

            LightBarFieldLayout(bar, fieldWidth);
            ImGui::DragFloat("##kelvin", kelvin, KELVIN_DRAG_SPEED, MIN_KELVIN, MAX_KELVIN, "%.0f K",
                             ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);

            LightBarLabel(bar, label);
            ImGui::EndGroup();

            const bool changed{ *kelvin != before };
            MarkEdited(changed);
            ImGui::PopID();
            return changed;
        }

        bool IntensityEditImpl(const char* label, float* intensity, ImOkLightUnit unit, ImOkLightEditFlags flags,
                               const ImVec2* verticalSize, const char* note = nullptr)
        {
            const Internal::LightUnitInfo& info{ Internal::GetLightUnitInfo(unit) };
            const bool speedTweaks{ (flags & ImOkLightEditFlags_NoSpeedTweaks) == 0 };
            const int referenceCount{ ((flags & ImOkLightEditFlags_NoReferences) == 0) ? info.referenceCount : 0 };
            ImGui::PushID(label);
            const float before{ *intensity };
            const float fieldWidth{ ImGui::CalcTextSize("0000000 nits").x + ImGui::GetStyle().FramePadding.x * 2.0f };

            ImGui::BeginGroup();

            const LightBar bar{ LightBarButton(fieldWidth, verticalSize) };
            if (ImGui::IsItemActive())
            {
                *intensity = Internal::BarPositionToIntensity(
                    DragBarPosition(bar, Internal::IntensityToBarPosition(*intensity, info), speedTweaks), info);
            }
            ReferenceTooltip(info.description, info.references, referenceCount, note);

            ImDrawList* drawList{ ImGui::GetWindowDrawList() };
            AddRectGradient(drawList, bar.min, bar.max, bar.vertical ? GradientAxis::Vertical : GradientAxis::Horizontal,
                            IntensityGradientColorAt, &bar.vertical, 0);
            for (int i{ 0 }; i < referenceCount; ++i)
            {
                const float value{ info.references[i].value };
                if (value >= info.minValue && value <= info.maxValue)
                {
                    const float position{ Internal::IntensityToBarPosition(value, info) };
                    AddReferenceTick(drawList, bar, position, IntensityRampLightness(position));
                }
            }
            AddLightBarMarker(drawList, bar, Internal::IntensityToBarPosition(*intensity, info));

            // Linear drag stepping with the value: ImGui's logarithmic drag reads its zero step
            // from the format, which here changes with the value
            char format[32]{};
            std::snprintf(format, sizeof(format), "%%.%df%s%s", Internal::IntensityDecimals(*intensity),
                          (info.symbol[0] != '\0') ? " " : "", info.symbol);
            const float speed{ (info.logMin <= 0.0f)
                ? EV_DRAG_SPEED
                : std::fmax(std::fabs(*intensity) * INTENSITY_DRAG_FRACTION, info.logMin * INTENSITY_DRAG_MIN_STEPS) };
            LightBarFieldLayout(bar, fieldWidth);
            ImGui::DragFloat("##intensity", intensity, speed, info.minValue, info.maxValue, format,
                             ImGuiSliderFlags_AlwaysClamp);

            LightBarLabel(bar, label);
            ImGui::EndGroup();

            const bool changed{ *intensity != before };
            MarkEdited(changed);
            ImGui::PopID();
            return changed;
        }


        constexpr Surface LIGHT_SURFACE{ Surface::OkhsvSaturationValue };

        ImOkColorEditFlags LightColorFlags(ImOkLightEditFlags flags)
        {
            ImOkColorEditFlags colorFlags{ ImOkColorEditFlags_NoBrightness };
            if (flags & ImOkLightEditFlags_NoDragDrop)
            {
                colorFlags |= ImOkColorEditFlags_NoDragDrop;
            }
            if (flags & ImOkLightEditFlags_NoSpeedTweaks)
            {
                colorFlags |= ImOkColorEditFlags_NoSpeedTweaks;
            }
            if (flags & ImOkLightEditFlags_NoTooltip)
            {
                colorFlags |= ImOkColorEditFlags_NoTooltip;
            }
            return colorFlags;
        }

        const char* LightKindNote(ImOkLightKind kind, ImOkLightUnit unit)
        {
            return (unit == ImOkLightUnit_Unitless) ? nullptr : Internal::GetLightKindInfo(kind).note;
        }

        SurfaceValue LoadLightColor(const float col[3], ImOkStoredAs storage)
        {
            SurfaceValue value{ LoadSurfaceState(col, storage, LIGHT_SURFACE) };
            value.y = 1.0f;
            return value;
        }

        // original: col[0..2], kelvin, intensity when LightEdit's popup opened, or nullptr
        EditKind LightPickerBody(ImOkLightKind kind, SurfaceValue& value, float col[3], ImOkStoredAs storage,
                                 float* kelvin, float* intensity, ImOkLightUnit unit, ImOkLightEditFlags flags,
                                 const float* original)
        {
            const float spacing{ ImGui::GetStyle().ItemInnerSpacing.x };
            const float frame{ ImGui::GetFrameHeight() };
            const float totalWidth{ ImGui::CalcItemWidth() };
            const ImVec2 previewSize{ frame * 2.0f, frame };
            const float pickerWidth{ std::fmax(frame, totalWidth - previewSize.x - spacing) };
            const ImOkColorEditFlags colorFlags{ LightColorFlags(flags) };
            const ImGuiColorEditFlags previewFlags{ ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop };
            EditKind edit{ EditKind::None };

            ImGui::BeginGroup();
            ImGui::PushItemWidth(pickerWidth);
            if (PickSurfaceValue(LIGHT_SURFACE, value, nullptr, true, (flags & ImOkLightEditFlags_NoSpeedTweaks) == 0))
            {
                edit = EditKind::Value;
            }
            edit = Merge(edit, DisplayFields(DisplayMode::Okhsv, LIGHT_SURFACE, value, col, storage, pickerWidth, nullptr, true));
            ImGui::PopItemWidth();
            ImGui::EndGroup();
            edit = Merge(edit, DropOnStored(col, nullptr, storage, colorFlags));

            // The drop target submits no item, so this menu is still the group's
            if (ImGui::BeginPopupContextItem("color options"))
            {
                CopyMenuItems(col, nullptr, storage, false);
                ImGui::EndPopup();
            }

            // Only Color is a drag source: Light and Original include the temperature
            const LinearSrgb color{ OkLabToLinearSrgb(SurfaceValueToOkLab(LIGHT_SURFACE, value)) };
            ImGui::SameLine(0.0f, spacing);
            ImGui::BeginGroup();
            ImGui::TextUnformatted("Color");
            const ImVec4 colorShown{ LinearSrgbToImVec4(color) };
            ImGui::ColorButton("##color", colorShown, previewFlags, previewSize);
            ColorDragSource(col, nullptr, storage, colorShown, previewFlags, colorFlags);
            ColorSwatchTooltip(col, nullptr, storage, colorShown, previewFlags, colorFlags);

            ImGui::TextUnformatted("Light");
            const ImVec4 lightShown{ LinearSrgbToImVec4(Internal::LightSwatchColor(color, kelvin)) };
            ImGui::ColorButton("##light", lightShown, previewFlags, previewSize);
            LightSwatchTooltip(color, kelvin, *intensity, unit, lightShown, previewFlags, flags);

            if (original != nullptr)
            {
                const float originalKelvin{ original[3] };
                const LinearSrgb originalColor{ StoredToLinearSrgb(original, storage) };
                const ImVec4 originalShown{ LinearSrgbToImVec4(Internal::LightSwatchColor(
                    originalColor, (kelvin != nullptr) ? &originalKelvin : nullptr)) };
                ImGui::TextUnformatted("Original");
                const bool restoreOriginal{ ImGui::ColorButton("##original", originalShown, previewFlags, previewSize) };
                LightSwatchTooltip(originalColor, (kelvin != nullptr) ? &originalKelvin : nullptr, original[4], unit,
                                   originalShown, previewFlags, flags);
                if (restoreOriginal)
                {
                    col[0] = original[0];
                    col[1] = original[1];
                    col[2] = original[2];
                    if (kelvin != nullptr)
                    {
                        *kelvin = original[3];
                    }
                    *intensity = original[4];
                    edit = EditKind::Stored;
                }
            }
            ImGui::EndGroup();

            ImGui::PushItemWidth(totalWidth);
            if (kelvin != nullptr)
            {
                TemperatureEditImpl("##temperature", kelvin, flags, nullptr);
            }
            IntensityEditImpl("##intensity", intensity, unit, flags, nullptr, LightKindNote(kind, unit));
            ImGui::PopItemWidth();

            char passes[Internal::COPY_BUFFER_SIZE]{};
            Internal::LightPassesText(color, kelvin, *intensity, unit, passes);
            ImGui::TextUnformatted(passes);

            return edit;
        }

        bool LightEditImpl(const char* label, ImOkLightKind kind, float col[3], ImOkStoredAs storage, float* kelvin,
                           float* intensity, ImOkLightUnit unit, ImOkLightEditFlags flags)
        {
            IM_ASSERT(Internal::IsLightUnitValid(kind, unit) && "This ImOkLightUnit is not valid for this ImOkLightKind (see ImOkLightKind).");
            ImGui::PushID(label);
            ImGui::BeginGroup();

            const float spacing{ ImGui::GetStyle().ItemInnerSpacing.x };
            const float squareSize{ ImGui::GetFrameHeight() };
            const float barWidth{ std::fmax(1.0f, ImGui::CalcItemWidth() - squareSize - spacing) };
            const char* labelEnd{ FindLabelEnd(label) };

            // Here: inside the popup, GetID and GetStateStorage refer to the popup window
            ImGuiStorage* stateStorage{ ImGui::GetStateStorage() };
            const ImGuiID originalIds[5]{ ImGui::GetID("original.0"), ImGui::GetID("original.1"), ImGui::GetID("original.2"),
                                          ImGui::GetID("original.kelvin"), ImGui::GetID("original.intensity") };

            SurfaceValue value{ LoadLightColor(col, storage) };
            const float kelvinBefore{ (kelvin != nullptr) ? *kelvin : 0.0f };
            const float intensityBefore{ *intensity };
            const ImOkColorEditFlags colorFlags{ LightColorFlags(flags) };
            EditKind edit{ EditKind::None };

            // No drag source: the swatch includes the temperature
            const LinearSrgb color{ OkLabToLinearSrgb(SurfaceValueToOkLab(LIGHT_SURFACE, value)) };
            const ImGuiColorEditFlags swatchFlags{ ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop };
            const ImVec4 swatchShown{ LinearSrgbToImVec4(Internal::LightSwatchColor(color, kelvin)) };
            if (ImGui::ColorButton("##swatch", swatchShown, swatchFlags, ImVec2(squareSize, squareSize)))
            {
                const float current[5]{ col[0], col[1], col[2], kelvinBefore, intensityBefore };
                for (int i{ 0 }; i < 5; ++i)
                {
                    stateStorage->SetFloat(originalIds[i], current[i]);
                }
                ImGui::OpenPopup("picker");
            }

            const ImVec2 popupPos{ ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + ImGui::GetStyle().ItemSpacing.y };
            edit = Merge(edit, DropOnStored(col, nullptr, storage, colorFlags));
            LightSwatchTooltip(color, kelvin, *intensity, unit, swatchShown, swatchFlags, flags);

            ImGui::SameLine(0.0f, spacing);
            ImGui::PushItemWidth(barWidth);
            IntensityEditImpl("##intensity", intensity, unit, flags, nullptr, LightKindNote(kind, unit));
            ImGui::PopItemWidth();

            // Here, not at OpenPopup: the tooltips open windows in between and would take the position
            ImGui::SetNextWindowPos(popupPos, ImGuiCond_Appearing);
            if (ImGui::BeginPopup("picker"))
            {
                if (labelEnd != label)
                {
                    ImGui::TextUnformatted(label, labelEnd);
                    ImGui::Separator();
                }
                float original[5]{};
                for (int i{ 0 }; i < 5; ++i)
                {
                    original[i] = stateStorage->GetFloat(originalIds[i]);
                }
                ImGui::PushItemWidth(ImGui::GetFrameHeight() * LIGHT_POPUP_WIDTH_FRAMES);
                edit = Merge(edit, LightPickerBody(kind, value, col, storage, kelvin, intensity, unit, flags, original));
                ImGui::PopItemWidth();
                ImGui::EndPopup();
            }

            if (labelEnd != label)
            {
                ImGui::SameLine(0.0f, spacing);
                ImGui::TextUnformatted(label, labelEnd);
            }

            ImGui::EndGroup();

            ApplyEdit(edit, LIGHT_SURFACE, value, col, storage, true);
            SaveSurfaceState(LIGHT_SURFACE, value, col);
            const bool changed{ edit != EditKind::None || (kelvin != nullptr && *kelvin != kelvinBefore)
                                || *intensity != intensityBefore };
            MarkEdited(changed);
            ImGui::PopID();
            return changed;
        }

        bool LightPickerImpl(const char* label, ImOkLightKind kind, float col[3], ImOkStoredAs storage, float* kelvin,
                             float* intensity, ImOkLightUnit unit, ImOkLightEditFlags flags)
        {
            IM_ASSERT(Internal::IsLightUnitValid(kind, unit) && "This ImOkLightUnit is not valid for this ImOkLightKind (see ImOkLightKind).");
            ImGui::PushID(label);

            SurfaceValue value{ LoadLightColor(col, storage) };
            const float kelvinBefore{ (kelvin != nullptr) ? *kelvin : 0.0f };
            const float intensityBefore{ *intensity };

            ImGui::BeginGroup();
            const EditKind edit{ LightPickerBody(kind, value, col, storage, kelvin, intensity, unit, flags, nullptr) };
            ImGui::EndGroup();

            ApplyEdit(edit, LIGHT_SURFACE, value, col, storage, true);
            SaveSurfaceState(LIGHT_SURFACE, value, col);
            const bool changed{ edit != EditKind::None || (kelvin != nullptr && *kelvin != kelvinBefore)
                                || *intensity != intensityBefore };
            MarkEdited(changed);
            ImGui::PopID();
            return changed;
        }
    }

    ImU32 LinearSrgbToImU32(const LinearSrgb& linear, float alpha, ImOkDrawFlags flags)
    {
        const EncodedSrgb encoded{ LinearSrgbToDisplayEncoded(linear, flags) };
        return IM_COL32(ChannelToByte(encoded.r), ChannelToByte(encoded.g),
                        ChannelToByte(encoded.b), ChannelToByte(alpha));
    }

    ImVec4 LinearSrgbToImVec4(const LinearSrgb& linear, float alpha, ImOkDrawFlags flags)
    {
        const EncodedSrgb encoded{ LinearSrgbToDisplayEncoded(linear, flags) };
        return {encoded.r, encoded.g, encoded.b, alpha};
    }

    LinearSrgb ImVec4ToLinearSrgb(const ImVec4& encoded)
    {
        return EncodedSrgbToLinearSrgb({ encoded.x, encoded.y, encoded.z });
    }

    LinearSrgb ImU32ToLinearSrgb(ImU32 encoded)
    {
        const float r{ static_cast<float>((encoded >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f };
        const float g{ static_cast<float>((encoded >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f };
        const float b{ static_cast<float>((encoded >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f };
        return EncodedSrgbToLinearSrgb({ r, g, b });
    }

    void AddRectGradientLinearSrgb(ImDrawList* drawList, const ImVec2& pMin, const ImVec2& pMax,
                                   const LinearSrgb& left, const LinearSrgb& right, ImOkDrawFlags flags)
    {
        const LinearSrgbGradient gradient{ left, right };
        AddRectGradient(drawList, pMin, pMax, GradientAxis::Horizontal, LinearSrgbGradientColorAt, &gradient, flags);
    }

    void AddRectGradientOkLab(ImDrawList* drawList, const ImVec2& pMin, const ImVec2& pMax,
                              const OkLab& left, const OkLab& right, ImOkDrawFlags flags)
    {
        const OkLabGradient gradient{ left, right };
        AddRectGradient(drawList, pMin, pMax, GradientAxis::Horizontal, OkLabGradientColorAt, &gradient, flags);
    }

    void AddRectGradientOkLCh(ImDrawList* drawList, const ImVec2& pMin, const ImVec2& pMax,
                              const OkLCh& left, const OkLCh& right, HueDirection direction,
                              ImOkDrawFlags flags)
    {
        const OkLChGradient gradient{ left, right, direction };
        AddRectGradient(drawList, pMin, pMax, GradientAxis::Horizontal, OkLChGradientColorAt, &gradient, flags);
    }

    void AddRectOkhsvSaturationValue(ImDrawList* drawList, const ImVec2& pMin, const ImVec2& pMax,
                                     float hue, ImOkDrawFlags flags)
    {
        const OkhsvSaturationValue surface{ hue };
        AddRectSurface(drawList, pMin, pMax, OkhsvSaturationValueColorAt, &surface, flags);
    }

    void AddRectOkhslSaturationLightness(ImDrawList* drawList, const ImVec2& pMin, const ImVec2& pMax,
                                         float hue, ImOkDrawFlags flags)
    {
        const OkhslSaturationLightness surface{ hue };
        AddRectSurface(drawList, pMin, pMax, OkhslSaturationLightnessColorAt, &surface, flags);
    }

    void AddRectOkhsvHueSaturation(ImDrawList* drawList, const ImVec2& pMin, const ImVec2& pMax,
                                   ImOkDrawFlags flags)
    {
        // The gamut has a corner at each primary: a column on each side keeps it in a hair-wide
        // cell. Not on the hue itself: at blue's, the cusp jumps.
        const LinearSrgb primaries[3]{ { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };
        float columns[6]{};
        for (int i{ 0 }; i < 3; ++i)
        {
            const float hue{ OkLabToOkLCh(LinearSrgbToOkLab(primaries[i])).h };
            columns[i * 2] = (hue - PRIMARY_HUE_MARGIN) / 360.0f;
            columns[i * 2 + 1] = (hue + PRIMARY_HUE_MARGIN) / 360.0f;
        }
        AddRectSurface(drawList, pMin, pMax, OkhsvHueSaturationColorAt, nullptr, flags, columns, 6);
    }

    void AddRectHueBar(ImDrawList* drawList, const ImVec2& pMin, const ImVec2& pMax, ImOkDrawFlags flags)
    {
        AddRectGradient(drawList, pMin, pMax, GradientAxis::Vertical, HueBarColorAt, nullptr, flags);
    }

    void AddRectAlphaBar(ImDrawList* drawList, const ImVec2& pMin, const ImVec2& pMax,
                         const LinearSrgb& color, ImOkDrawFlags flags)
    {
        const float width{ pMax.x - pMin.x };
        if (width <= 0.0f || pMax.y <= pMin.y)
        {
            return;
        }

        AddCheckerboard(drawList, pMin, pMax, width * 0.5f);

        // One quad is exact: only alpha changes, and it interpolates linearly
        const ImU32 opaque{ LinearSrgbToImU32(color, StyleAlpha(), flags) };
        const ImU32 transparent{ opaque & ~IM_COL32_A_MASK };
        drawList->AddRectFilledMultiColor(pMin, pMax, opaque, opaque, transparent, transparent);
    }

    bool ColorPickerOkhsv(const char* label, Okhsv* color, float* alpha, ImOkColorEditFlags flags)
    {
        IM_ASSERT((flags & ~(ImOkColorEditFlags_NoDragDrop | ImOkColorEditFlags_NoSpeedTweaks)) == 0 &&
                   "Native pickers only take ImOkColorEditFlags_NoDragDrop and _NoSpeedTweaks: their surface is fixed, and they have no fields.");
        ImGui::PushID(label);
        const Okhsv before{ *color };
        const float alphaBefore{ (alpha != nullptr) ? *alpha : 0.0f };

        ImGui::BeginGroup();
        HueSquarePicker(OKHSV_SATURATION_VALUE, color->h, color->s, color->v, alpha, (flags & ImOkColorEditFlags_NoSpeedTweaks) == 0);
        ImGui::EndGroup();

        float data[4]{};
        const int count{ AcceptColorDrop(data, flags) };
        if (count > 0 && Internal::IsFinitePayload(data, count))
        {
            *color = Internal::OkLabToOkhsvKeeping(LinearSrgbToOkLab(Internal::PayloadToLinearSrgb(data)), *color);
            Internal::ApplyPayloadAlpha(data, count, alpha);
        }

        const bool changed{ color->h != before.h || color->s != before.s || color->v != before.v
                            || (alpha != nullptr && *alpha != alphaBefore) };
        MarkEdited(changed);
        ImGui::PopID();
        return changed;
    }

    bool ColorPickerOkhsl(const char* label, Okhsl* color, float* alpha, ImOkColorEditFlags flags)
    {
        IM_ASSERT((flags & ~(ImOkColorEditFlags_NoDragDrop | ImOkColorEditFlags_NoSpeedTweaks)) == 0 &&
                   "Native pickers only take ImOkColorEditFlags_NoDragDrop and _NoSpeedTweaks: their surface is fixed, and they have no fields.");
        ImGui::PushID(label);
        const Okhsl before{ *color };
        const float alphaBefore{ (alpha != nullptr) ? *alpha : 0.0f };

        ImGui::BeginGroup();
        HueSquarePicker(OKHSL_SATURATION_LIGHTNESS, color->h, color->s, color->l, alpha, (flags & ImOkColorEditFlags_NoSpeedTweaks) == 0);
        ImGui::EndGroup();

        float data[4]{};
        const int count{ AcceptColorDrop(data, flags) };
        if (count > 0 && Internal::IsFinitePayload(data, count))
        {
            *color = Internal::OkLabToOkhslKeeping(LinearSrgbToOkLab(Internal::PayloadToLinearSrgb(data)), *color);
            Internal::ApplyPayloadAlpha(data, count, alpha);
        }

        const bool changed{ color->h != before.h || color->s != before.s || color->l != before.l
                            || (alpha != nullptr && *alpha != alphaBefore) };
        MarkEdited(changed);
        ImGui::PopID();
        return changed;
    }

    bool ColorPicker3(const char* label, float col[3], ImOkStoredAs storage, ImOkColorEditFlags flags)
    {
        return ColorPickerImpl(label, col, nullptr, storage, flags);
    }

    bool ColorEdit3(const char* label, float col[3], ImOkStoredAs storage, ImOkColorEditFlags flags)
    {
        return ColorEditImpl(label, col, nullptr, storage, flags);
    }

    bool ColorPicker4(const char* label, float col[4], ImOkStoredAs storage, ImOkColorEditFlags flags)
    {
        return ColorPickerImpl(label, col, &col[3], storage, flags);
    }

    bool ColorEdit4(const char* label, float col[4], ImOkStoredAs storage, ImOkColorEditFlags flags)
    {
        return ColorEditImpl(label, col, &col[3], storage, flags);
    }

    bool TemperatureEdit(const char* label, float* kelvin, ImOkLightEditFlags flags)
    {
        return TemperatureEditImpl(label, kelvin, flags, nullptr);
    }

    bool VTemperatureEdit(const char* label, const ImVec2& size, float* kelvin, ImOkLightEditFlags flags)
    {
        return TemperatureEditImpl(label, kelvin, flags, &size);
    }

    bool IntensityEdit(const char* label, float* intensity, ImOkLightUnit unit, ImOkLightEditFlags flags)
    {
        return IntensityEditImpl(label, intensity, unit, flags, nullptr);
    }

    bool VIntensityEdit(const char* label, const ImVec2& size, float* intensity, ImOkLightUnit unit, ImOkLightEditFlags flags)
    {
        return IntensityEditImpl(label, intensity, unit, flags, &size);
    }

    bool LightEdit(const char* label, ImOkLightKind kind, float col[3], ImOkStoredAs storage, float* kelvin,
                   float* intensity, ImOkLightUnit unit, ImOkLightEditFlags flags)
    {
        return LightEditImpl(label, kind, col, storage, kelvin, intensity, unit, flags);
    }

    bool LightPicker(const char* label, ImOkLightKind kind, float col[3], ImOkStoredAs storage, float* kelvin,
                     float* intensity, ImOkLightUnit unit, ImOkLightEditFlags flags)
    {
        return LightPickerImpl(label, kind, col, storage, kelvin, intensity, unit, flags);
    }
}