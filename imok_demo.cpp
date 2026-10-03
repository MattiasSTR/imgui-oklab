// ImOk demo window
// See LICENSE.txt for copyright and licensing details (standard MIT License).
// Optional: add this file to your build to get ImOk::ShowDemoWindow().

#include "imok.h"

#include <cmath>
#include <cstring>

namespace ImOk
{
    namespace
    {
        void ReserveBar(float height, ImVec2& pMin, ImVec2& pMax)
        {
            pMin = ImGui::GetCursorScreenPos();
            pMax = ImVec2(pMin.x + ImGui::GetContentRegionAvail().x, pMin.y + height);
            ImGui::Dummy(ImVec2(pMax.x - pMin.x, height));
        }

        void EncodedFloatsToLinear(const float in[3], float out[3])
        {
            const LinearSrgb c{ EncodedSrgbToLinearSrgb({ in[0], in[1], in[2] }) };
            out[0] = c.r;
            out[1] = c.g;
            out[2] = c.b;
        }

        void LinearFloatsToEncoded(const float in[3], float out[3])
        {
            const EncodedSrgb c{ LinearSrgbToEncodedSrgb({ in[0], in[1], in[2] }) };
            out[0] = c.r;
            out[1] = c.g;
            out[2] = c.b;
        }

        float SideBySideWidth()
        {
            const float available{ ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x };
            return std::fmin(ImGui::GetFrameHeight() * 12.0f, available * 0.5f);
        }

        // Hex from the swatch's own ImVec4, so both show the same clipped color
        void ResultSwatch(const OkLab& color, float alpha = 1.0f)
        {
            const ImVec4 shown{ LinearSrgbToImVec4(OkLabToLinearSrgb(color), alpha) };
            const float frame{ ImGui::GetFrameHeight() };
            ImGui::ColorButton("##result", shown, ImGuiColorEditFlags_NoTooltip, ImVec2(frame * 2.0f, frame));
            ImGui::SameLine();
            char hex[HEX_BUFFER_SIZE]{};
            EncodedSrgbToHex({ shown.x, shown.y, shown.z }, hex);
            ImGui::TextUnformatted(hex);
        }

        void Tldr(const char* text)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
            ImGui::TextWrapped("TL;DR: %s", text);
            ImGui::PopStyleColor();
        }

        // ImGui interpolates each segment in encoded sRGB, invisible at this density
        constexpr int SWEEP_SEGMENTS{ 128 };

        using OkLabAt = OkLab (*)(float t);

        void Practice(const char* rule, const char* why)
        {
            ImGui::SeparatorText(rule);
            ImGui::TextWrapped("%s", why);
        }

        ImU32 EncodedToImU32(float r, float g, float b)
        {
            return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, 1.0f));
        }

        ImU32 LightnessToImU32(const OkLab& color)
        {
            return LinearSrgbToImU32(OkLabToLinearSrgb({ color.L, 0.0f, 0.0f }));
        }

        void AddThreeBands(ImDrawList* drawList, const ImVec2& pMin, const ImVec2& pMax, ImU32 a, ImU32 both, ImU32 b)
        {
            const float third{ (pMax.x - pMin.x) / 3.0f };
            drawList->AddRectFilled(pMin, ImVec2(pMin.x + third, pMax.y), a);
            drawList->AddRectFilled(ImVec2(pMin.x + third, pMin.y), ImVec2(pMin.x + third * 2.0f, pMax.y), both);
            drawList->AddRectFilled(ImVec2(pMin.x + third * 2.0f, pMin.y), pMax, b);
        }

        void SweepWithLightness(float height, OkLabAt colorAt)
        {
            ImVec2 pMin{};
            ImVec2 pMax{};
            ReserveBar(height, pMin, pMax);

            ImDrawList* drawList{ ImGui::GetWindowDrawList() };
            const float width{ pMax.x - pMin.x };
            const float split{ pMin.y + height * 0.65f };
            for (int i{ 0 }; i < SWEEP_SEGMENTS; ++i)
            {
                const float t0{ static_cast<float>(i) / SWEEP_SEGMENTS };
                const float t1{ static_cast<float>(i + 1) / SWEEP_SEGMENTS };
                const OkLab c0{ colorAt(t0) };
                const OkLab c1{ colorAt(t1) };
                const float x0{ pMin.x + width * t0 };
                const float x1{ pMin.x + width * t1 };

                const ImU32 color0{ LinearSrgbToImU32(OkLabToLinearSrgb(c0)) };
                const ImU32 color1{ LinearSrgbToImU32(OkLabToLinearSrgb(c1)) };
                drawList->AddRectFilledMultiColor(ImVec2(x0, pMin.y), ImVec2(x1, split), color0, color1, color1, color0);

                const ImU32 gray0{ LightnessToImU32(c0) };
                const ImU32 gray1{ LightnessToImU32(c1) };
                drawList->AddRectFilledMultiColor(ImVec2(x0, split), ImVec2(x1, pMax.y), gray0, gray1, gray1, gray0);
            }
        }

        void SwatchesWithLightness(const OkLab* colors, int count)
        {
            const float size{ ImGui::GetFrameHeight() * 1.5f };
            const float spacing{ ImGui::GetStyle().ItemInnerSpacing.x };
            const ImVec2 origin{ ImGui::GetCursorScreenPos() };
            ImDrawList* drawList{ ImGui::GetWindowDrawList() };

            for (int i{ 0 }; i < count; ++i)
            {
                const ImVec2 p{ origin.x + static_cast<float>(i) * (size + spacing), origin.y };
                drawList->AddRectFilled(p, ImVec2(p.x + size, p.y + size), LinearSrgbToImU32(OkLabToLinearSrgb(colors[i])));
                drawList->AddRectFilled(ImVec2(p.x, p.y + size), ImVec2(p.x + size, p.y + size * 1.5f), LightnessToImU32(colors[i]));
            }
            ImGui::Dummy(ImVec2(static_cast<float>(count) * (size + spacing) - spacing, size * 1.5f));
        }

        using ChannelFn = float (*)(float v);

        float EncodeChannel(float v)
        {
            return LinearSrgbToEncodedSrgb({ v, v, v }).r;
        }

        float DecodeChannel(float v)
        {
            return EncodedSrgbToLinearSrgb({ v, v, v }).r;
        }

        // shown(v): what lands in the 8-bit vertex color. One-pixel columns, so no interpolation
        // hides or adds steps.
        void GrayRamp(float height, float maxValue, ChannelFn shown)
        {
            ImVec2 pMin{};
            ImVec2 pMax{};
            ReserveBar(height, pMin, pMax);

            ImDrawList* drawList{ ImGui::GetWindowDrawList() };
            const float left{ std::floor(pMin.x) };
            const int columns{ static_cast<int>(pMax.x - left) };
            for (int x{ 0 }; x < columns; ++x)
            {
                const float v{ maxValue * (static_cast<float>(x) + 0.5f) / static_cast<float>(columns) };
                const float value{ shown(v) };
                const float columnX{ left + static_cast<float>(x) };
                drawList->AddRectFilled(ImVec2(columnX, pMin.y), ImVec2(columnX + 1.0f, pMax.y),
                                        EncodedToImU32(value, value, value));
            }
        }

        // sampled(mask): what the shader reads for a mask stored as mask. One-pixel columns, as GrayRamp.
        void MaskRamp(float height, const LinearSrgb& from, const LinearSrgb& to, ChannelFn sampled)
        {
            ImVec2 pMin{};
            ImVec2 pMax{};
            ReserveBar(height, pMin, pMax);

            ImDrawList* drawList{ ImGui::GetWindowDrawList() };
            const float left{ std::floor(pMin.x) };
            const int columns{ static_cast<int>(pMax.x - left) };
            for (int x{ 0 }; x < columns; ++x)
            {
                const float mask{ (static_cast<float>(x) + 0.5f) / static_cast<float>(columns) };
                const float columnX{ left + static_cast<float>(x) };
                drawList->AddRectFilled(ImVec2(columnX, pMin.y), ImVec2(columnX + 1.0f, pMax.y),
                                        LinearSrgbToImU32(LerpLinearSrgb(from, to, sampled(mask))));
            }

            const float middle{ std::floor(left + static_cast<float>(columns) * 0.5f) + 0.5f };
            drawList->AddLine(ImVec2(middle, pMin.y), ImVec2(middle, pMax.y), ImGui::GetColorU32(ImGuiCol_Text));
        }

        // A filtered sample between an opaque texel (left) and a transparent one holding hidden
        // (right), over background. One-pixel columns, as GrayRamp.
        void EdgeRamp(float height, const LinearSrgb& opaque, const LinearSrgb& hidden, const LinearSrgb& background,
                      bool premultiplied)
        {
            ImVec2 pMin{};
            ImVec2 pMax{};
            ReserveBar(height, pMin, pMax);

            ImDrawList* drawList{ ImGui::GetWindowDrawList() };
            const float left{ std::floor(pMin.x) };
            const int columns{ static_cast<int>(pMax.x - left) };
            for (int x{ 0 }; x < columns; ++x)
            {
                const float t{ (static_cast<float>(x) + 0.5f) / static_cast<float>(columns) };
                // Premultiplied, the transparent texel adds nothing; straight, its color is mixed in
                const LinearSrgb color{ premultiplied ? opaque : LerpLinearSrgb(opaque, hidden, t) };
                const float columnX{ left + static_cast<float>(x) };
                drawList->AddRectFilled(ImVec2(columnX, pMin.y), ImVec2(columnX + 1.0f, pMax.y),
                                        LinearSrgbToImU32(LerpLinearSrgb(background, color, 1.0f - t)));
            }
        }

        // ImGui's own colors; encodedTwice shows them as an _SRGB view would, encoded values encoded again
        void StyleSwatches(bool encodedTwice)
        {
            constexpr ImGuiCol COLORS[]{ ImGuiCol_WindowBg, ImGuiCol_FrameBg, ImGuiCol_Button, ImGuiCol_Header,
                                         ImGuiCol_CheckMark, ImGuiCol_Text };
            const ImVec2 size{ ImGui::GetFrameHeight() * 1.5f, ImGui::GetFrameHeight() };

            ImGui::PushID(encodedTwice ? "twice" : "once");
            for (int i{ 0 }; i < IM_COUNTOF(COLORS); ++i)
            {
                const ImVec4 style{ ImGui::GetStyleColorVec4(COLORS[i]) };
                ImVec4 shown{ style.x, style.y, style.z, 1.0f };
                if (encodedTwice)
                {
                    const EncodedSrgb twice{ LinearSrgbToEncodedSrgb({ style.x, style.y, style.z }) };
                    shown = ImVec4(twice.r, twice.g, twice.b, 1.0f);
                }

                if (i > 0)
                {
                    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                }
                ImGui::PushID(i);
                ImGui::ColorButton("##style", shown, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, size);
                ImGui::PopID();
            }
            ImGui::PopID();
        }

        void LightnessReadout(const float colorEncoded[3])
        {
            const OkLab lab{ LinearSrgbToOkLab(EncodedSrgbToLinearSrgb({ colorEncoded[0], colorEncoded[1], colorEncoded[2] })) };
            const ImVec2 size{ ImGui::GetFrameHeight() * 2.0f, ImGui::GetFrameHeight() };
            ImGui::ColorButton("##color", ImVec4(colorEncoded[0], colorEncoded[1], colorEncoded[2], 1.0f), ImGuiColorEditFlags_NoTooltip, size);
            ImGui::SameLine();
            ImGui::ColorButton("##lightness", ImGui::ColorConvertU32ToFloat4(LightnessToImU32(lab)),
                               ImGuiColorEditFlags_NoTooltip, size);
            ImGui::SameLine();
            ImGui::Text("Oklab L %.2f", lab.L);
        }

        ImU32 LuminanceToImU32(const LinearSrgb& color)
        {
            const float Y{ RelativeLuminance(color) };
            return LinearSrgbToImU32({ Y, Y, Y });
        }

        using LinearSrgbAt = LinearSrgb (*)(float t);

        void SweepWithLuminance(float height, LinearSrgbAt colorAt, ImVec2& pMin, ImVec2& pMax)
        {
            ReserveBar(height, pMin, pMax);

            ImDrawList* drawList{ ImGui::GetWindowDrawList() };
            const float width{ pMax.x - pMin.x };
            const float split{ pMin.y + height * 0.65f };
            for (int i{ 0 }; i < SWEEP_SEGMENTS; ++i)
            {
                const float t0{ static_cast<float>(i) / SWEEP_SEGMENTS };
                const float t1{ static_cast<float>(i + 1) / SWEEP_SEGMENTS };
                const LinearSrgb c0{ colorAt(t0) };
                const LinearSrgb c1{ colorAt(t1) };
                const float x0{ pMin.x + width * t0 };
                const float x1{ pMin.x + width * t1 };

                const ImU32 color0{ LinearSrgbToImU32(c0) };
                const ImU32 color1{ LinearSrgbToImU32(c1) };
                drawList->AddRectFilledMultiColor(ImVec2(x0, pMin.y), ImVec2(x1, split), color0, color1, color1, color0);

                const ImU32 gray0{ LuminanceToImU32(c0) };
                const ImU32 gray1{ LuminanceToImU32(c1) };
                drawList->AddRectFilledMultiColor(ImVec2(x0, split), ImVec2(x1, pMax.y), gray0, gray1, gray1, gray0);
            }
        }
    }

    void ShowDemoWindow(bool* open)
    {
        const ImGuiViewport* mainViewport{ ImGui::GetMainViewport() };
        ImGui::SetNextWindowPos(ImVec2(mainViewport->WorkPos.x + 20, mainViewport->WorkPos.y + 20), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(600, 680), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("ImOk Demo", open))
        {
            ImGui::End();
            return;
        }

        const float frame{ ImGui::GetFrameHeight() };
        const float editWidth{ frame * 14.0f };

        ImGui::Text("ImOk %s (Dear ImGui %s)", IMOK_VERSION, IMGUI_VERSION);
        ImGui::Separator();
        ImGui::TextWrapped("A color passes through three kinds of numbers. 8-bit storage is encoded sRGB, "
                           "often called gamma-encoded. Physics is linear sRGB, and perception is Oklab. "
                           "Decode once where colors come in, work, and encode once where they go out. "
                           "The README explains why; Good practices shows it.");
        ImGui::Spacing();
        ImGui::TextWrapped("Every ImOk color widget is three independent choices:");
        ImGui::BulletText("Storage: what your floats hold, encoded or linear sRGB. Only you know which.");
        ImGui::BulletText("Picker: how the artist moves through colors.");
        ImGui::BulletText("Display: which numbers the fields show.");
        ImGui::TextWrapped("Sections 1 to 3 each change one and keep the others fixed. Right-click any "
                           "widget to change its picker or display. On any picker or bar, hold Alt for fine "
                           "steps or Shift for fast ones, as on ImGui's drags.");
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Good practices: decode once, work, encode once", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::PushID("practices");
            Tldr("Decode colors where they come in, do light math in linear sRGB and anything about how "
                 "colors look in Oklab, and encode once where they go out.");

            ImDrawList* drawList{ ImGui::GetWindowDrawList() };
            const float barHeight{ frame };
            ImVec2 pMin{};
            ImVec2 pMax{};

            if (ImGui::TreeNode("Entering: decode once"))
            {
                Tldr("Colors from other tools are encoded: decode them on import. Color textures are "
                     "encoded, and the sampler decodes them; data textures aren't encoded at all.");

                {
                    static char hex[HEX_BUFFER_SIZE]{ "#CC4C33" };

                    Practice("Colors from other tools: decode on import",
                             "Hex codes and colors from Photoshop, Figma or CSS are encoded sRGB. Decode them "
                             "once, when you import them, and store the linear sRGB floats the shader uses.");

                    ImGui::SetNextItemWidth(frame * 6.0f);
                    ImGui::InputText("Hex from another tool", hex, HEX_BUFFER_SIZE);

                    EncodedSrgb importedEncoded{};
                    if (HexToEncodedSrgb(hex, &importedEncoded))
                    {
                        const ImVec2 swatchSize{ frame * 2.0f, frame };
                        // The bug: the encoded numbers relabeled, with no decode
                        const LinearSrgb undecoded{ importedEncoded.r, importedEncoded.g, importedEncoded.b };
                        ImGui::ColorButton("##undecoded", LinearSrgbToImVec4(undecoded), ImGuiColorEditFlags_NoTooltip, swatchSize);
                        ImGui::SameLine();
                        ImGui::TextUnformatted("Wrong: its numbers used as linear sRGB");
                        ImGui::ColorButton("##decoded", LinearSrgbToImVec4(EncodedSrgbToLinearSrgb(importedEncoded)),
                                           ImGuiColorEditFlags_NoTooltip, swatchSize);
                        ImGui::SameLine();
                        ImGui::TextUnformatted("Right: decoded once (EncodedSrgbToLinearSrgb)");
                    }
                    else
                    {
                        ImGui::TextDisabled("Type a hex code, such as #CC4C33");
                    }
                }

                Practice("8-bit colors: encoded sRGB",
                         "Textures, vertex colors, ImU32. Encoding puts more of the 256 steps in the darks, "
                         "where the eye notices. Linear sRGB needs a float format, such as "
                         "R16G16B16A16_FLOAT or R11G11B10_FLOAT.");
                ImGui::TextUnformatted("The darkest 10% of light, stretched across the bar:");
                ImGui::TextUnformatted("Wrong: linear sRGB in 8 bits (27 levels)");
                GrayRamp(frame, 0.1f, [](float v) -> float
                {
                    return EncodeChannel(std::floor(v * 255.0f + 0.5f) / 255.0f);
                });
                ImGui::TextUnformatted("Right: encoded in 8 bits (90 levels)");
                GrayRamp(frame, 0.1f, [](float v) -> float { return EncodeChannel(v); });

                {
                    const LinearSrgb rustLinear{ 0.5f, 0.12f, 0.03f };
                    const LinearSrgb mossLinear{ 0.06f, 0.2f, 0.03f };

                    Practice("Data textures: not encoded",
                             "Normal maps, roughness and masks hold numbers, not colors. Turn their sRGB checkbox "
                             "off, or the sampler decodes them and a mask's 0.5 becomes 0.21.");
                    ImGui::TextUnformatted("A mask blending rust into moss, 0 at the left, 1 at the right:");
                    ImGui::TextUnformatted("Wrong: sampled as sRGB, halfway at 74%");
                    MaskRamp(frame, rustLinear, mossLinear, DecodeChannel);
                    ImGui::TextUnformatted("Right: sampled as data, halfway at the line");
                    MaskRamp(frame, rustLinear, mossLinear, [](float v) -> float { return v; });
                }

                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Physics: linear sRGB"))
            {
                Tldr("Anything that adds or mixes light happens in linear sRGB: summing lights, filtering, "
                     "alpha blending. Premultiply there too.");

                {
                    static float lightAEncoded[3]{ 0.5f, 0.5f, 0.5f };
                    static float lightBEncoded[3]{ 0.5f, 0.5f, 0.5f };

                    Practice("Summing lights: linear sRGB",
                             "Light adds up, and only linear sRGB numbers add like light. Two lights at encoded "
                             "0.5 sum to 1.0 in encoded math. In linear sRGB, 0.214 + 0.214 = 0.428, which "
                             "encodes to 0.686. Summed encoded, overlaps are far too bright.");

                    ImGui::PushItemWidth(editWidth);
                    ColorEdit3("Light A", lightAEncoded, ImOkStoredAs_EncodedSrgb);
                    ColorEdit3("Light B", lightBEncoded, ImOkStoredAs_EncodedSrgb);
                    ImGui::PopItemWidth();

                    const LinearSrgb lightALinear{ EncodedSrgbToLinearSrgb({ lightAEncoded[0], lightAEncoded[1], lightAEncoded[2] }) };
                    const LinearSrgb lightBLinear{ EncodedSrgbToLinearSrgb({ lightBEncoded[0], lightBEncoded[1], lightBEncoded[2] }) };
                    const ImU32 aColor{ EncodedToImU32(lightAEncoded[0], lightAEncoded[1], lightAEncoded[2]) };
                    const ImU32 bColor{ EncodedToImU32(lightBEncoded[0], lightBEncoded[1], lightBEncoded[2]) };
                    // Linear sRGB sum clamped, not clipped, so the two differ only in where the sum is done
                    const ImU32 encodedSum{ EncodedToImU32(lightAEncoded[0] + lightBEncoded[0], lightAEncoded[1] + lightBEncoded[1], lightAEncoded[2] + lightBEncoded[2]) };
                    const ImU32 linearSum{ LinearSrgbToImU32({ lightALinear.r + lightBLinear.r, lightALinear.g + lightBLinear.g, lightALinear.b + lightBLinear.b }, 1.0f, ImOkDrawFlags_ClampOutOfGamut) };

                    ImGui::TextUnformatted("Wrong: summed encoded (A | A + B | B)");
                    ReserveBar(barHeight, pMin, pMax);
                    AddThreeBands(drawList, pMin, pMax, aColor, encodedSum, bColor);

                    ImGui::TextUnformatted("Right: summed in linear sRGB");
                    ReserveBar(barHeight, pMin, pMax);
                    AddThreeBands(drawList, pMin, pMax, aColor, linearSum, bColor);
                }

                {
                    Practice("Filtering, mipmaps, blur: linear sRGB",
                             "Far away, a texture's texels blend. Black and white averaged as encoded numbers give "
                             "0.5, but the light reaching your eye averages to 0.5 in linear sRGB, which encodes "
                             "to 0.735. Encoded filtering darkens distant textures and blurred edges. An _SRGB "
                             "format decodes before it filters.");
                    ImGui::TextWrapped("Lean back or squint: the stripes blend into the right swatch. (Needs 100%% "
                                       "display scaling; scaled stripes are already blurred.)");

                    // Whole pixels, so every stripe is exactly one pixel of black or white
                    const ImVec2 cursor{ ImGui::GetCursorScreenPos() };
                    const ImVec2 origin{ std::floor(cursor.x), std::floor(cursor.y) };
                    const float size{ std::floor(frame * 3.0f) };
                    const float gap{ ImGui::GetStyle().ItemSpacing.x };

                    for (int y{ 0 }; y < static_cast<int>(size); ++y)
                    {
                        const ImU32 stripe{ (y % 2 == 0) ? IM_COL32_WHITE : IM_COL32_BLACK };
                        const float top{ origin.y + static_cast<float>(y) };
                        drawList->AddRectFilled(ImVec2(origin.x, top), ImVec2(origin.x + size, top + 1.0f), stripe);
                    }

                    const float wrongX{ origin.x + size + gap };
                    const float rightX{ wrongX + size + gap };
                    drawList->AddRectFilled(ImVec2(wrongX, origin.y), ImVec2(wrongX + size, origin.y + size),
                                            EncodedToImU32(0.5f, 0.5f, 0.5f));
                    drawList->AddRectFilled(ImVec2(rightX, origin.y), ImVec2(rightX + size, origin.y + size),
                                            LinearSrgbToImU32({ 0.5f, 0.5f, 0.5f }));
                    ImGui::Dummy(ImVec2(size * 3.0f + gap * 2.0f, size));
                    ImGui::TextUnformatted("Stripes | wrong: encoded average | right: linear sRGB average");
                }

                {
                    static float foregroundEncoded[3]{ 1.0f, 1.0f, 1.0f };
                    static float backgroundEncoded[3]{ 0.0f, 0.0f, 0.0f };

                    Practice("Alpha blending: linear sRGB",
                             "Blending mixes light: 50% white over black is 188 in linear sRGB, 128 blended "
                             "encoded. ImGui blends its UI encoded, ImOk's swatches included. Alpha itself is "
                             "never encoded: _SRGB formats decode only RGB.");
                    ImGui::PushItemWidth(editWidth);
                    ColorEdit3("Foreground", foregroundEncoded, ImOkStoredAs_EncodedSrgb);
                    ColorEdit3("Background", backgroundEncoded, ImOkStoredAs_EncodedSrgb);
                    ImGui::PopItemWidth();
                    ImGui::TextUnformatted("Alpha from 0 at the left to 1 at the right:");

                    // Blending at alpha t is a lerp from background to foreground, so a gradient shows every alpha
                    ImGui::TextUnformatted("Wrong: blended encoded (ImGui, a UNORM target)");
                    ReserveBar(barHeight, pMin, pMax);
                    {
                        const ImU32 bg{ EncodedToImU32(backgroundEncoded[0], backgroundEncoded[1], backgroundEncoded[2]) };
                        const ImU32 fg{ EncodedToImU32(foregroundEncoded[0], foregroundEncoded[1], foregroundEncoded[2]) };
                        drawList->AddRectFilledMultiColor(pMin, pMax, bg, fg, fg, bg);
                    }

                    ImGui::TextUnformatted("Right: blended in linear sRGB (an _SRGB target)");
                    ReserveBar(barHeight, pMin, pMax);
                    AddRectGradientLinearSrgb(drawList, pMin, pMax,
                                              EncodedSrgbToLinearSrgb({ backgroundEncoded[0], backgroundEncoded[1], backgroundEncoded[2] }),
                                              EncodedSrgbToLinearSrgb({ foregroundEncoded[0], foregroundEncoded[1], foregroundEncoded[2] }));
                }

                {
                    const LinearSrgb leafLinear{ 0.05f, 0.3f, 0.02f };
                    const LinearSrgb hiddenLinear{ 1.0f, 1.0f, 1.0f };
                    const LinearSrgb backgroundLinear{ 0.02f, 0.02f, 0.03f };

                    Practice("Premultiply before filtering, in linear sRGB",
                             "Widgets edit straight alpha, since at alpha 0 a premultiplied color is gone. "
                             "Premultiply the linear sRGB color where you upload it. Filtered with straight alpha, "
                             "the color hidden in transparent texels bleeds into the edges.");
                    ImGui::TextWrapped("A leaf's edge, filtered from an opaque texel to a transparent one that "
                                       "holds white:");
                    ImGui::TextUnformatted("Wrong: straight alpha, a pale fringe");
                    EdgeRamp(frame, leafLinear, hiddenLinear, backgroundLinear, false);
                    ImGui::TextUnformatted("Right: premultiplied, the leaf just fades");
                    EdgeRamp(frame, leafLinear, hiddenLinear, backgroundLinear, true);
                }

                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Perception: Oklab"))
            {
                Tldr("Anything about how colors look happens in Oklab: gradients, hue sweeps, lighter or "
                     "darker, sets that should look alike.");
                ImGui::TextWrapped("Each pair uses the same inputs; only the space the math is done in differs.");

                {
                    static float fromEncoded[3]{ 0.1f, 0.3f, 1.0f };
                    static float toEncoded[3]{ 1.0f, 0.85f, 0.0f };
                    static bool clampInsteadOfClip{ false };

                    Practice("Gradients and ramps: Oklab",
                             "UI bars, particle color over lifetime, heatmaps. Encoded sRGB sags into a dull "
                             "middle. Linear sRGB is right for light, but its middle looks too bright. Oklab "
                             "steps evenly.");

                    ImGui::PushItemWidth(editWidth);
                    ColorEdit3("From", fromEncoded, ImOkStoredAs_EncodedSrgb);
                    ColorEdit3("To", toEncoded, ImOkStoredAs_EncodedSrgb);
                    ImGui::PopItemWidth();
                    ImGui::Checkbox("Clamp out-of-gamut colors instead of clipping", &clampInsteadOfClip);

                    const ImOkDrawFlags flags{ clampInsteadOfClip ? ImOkDrawFlags_ClampOutOfGamut : ImOkDrawFlags_None };
                    const LinearSrgb fromLinear{ EncodedSrgbToLinearSrgb({ fromEncoded[0], fromEncoded[1], fromEncoded[2] }) };
                    const LinearSrgb toLinear{ EncodedSrgbToLinearSrgb({ toEncoded[0], toEncoded[1], toEncoded[2] }) };

                    ImGui::TextUnformatted("Wrong: encoded sRGB (ImGui's AddRectFilledMultiColor)");
                    ReserveBar(barHeight, pMin, pMax);
                    {
                        const ImU32 left{ EncodedToImU32(fromEncoded[0], fromEncoded[1], fromEncoded[2]) };
                        const ImU32 right{ EncodedToImU32(toEncoded[0], toEncoded[1], toEncoded[2]) };
                        drawList->AddRectFilledMultiColor(pMin, pMax, left, right, right, left);
                    }

                    ImGui::TextUnformatted("Wrong for looks: linear sRGB");
                    ReserveBar(barHeight, pMin, pMax);
                    AddRectGradientLinearSrgb(drawList, pMin, pMax, fromLinear, toLinear, flags);

                    ImGui::TextUnformatted("Right: Oklab");
                    ReserveBar(barHeight, pMin, pMax);
                    AddRectGradientOkLab(drawList, pMin, pMax, LinearSrgbToOkLab(fromLinear), LinearSrgbToOkLab(toLinear), flags);

                    ImGui::TextUnformatted("Also right: OkLCh, shorter hue. Keeps chroma, can leave the sRGB gamut");
                    ReserveBar(barHeight, pMin, pMax);
                    AddRectGradientOkLCh(drawList, pMin, pMax,
                                         OkLabToOkLCh(LinearSrgbToOkLab(fromLinear)),
                                         OkLabToOkLCh(LinearSrgbToOkLab(toLinear)),
                                         HueDirection::Shorter, flags);
                }

                {
                    Practice("Hue sweeps: Okhsl, lightness fixed",
                             "Rainbows, hue-shift effects, cycling colors. HSV holds V fixed, but V is not "
                             "lightness, so the sweep pulses between dark blue and glowing yellow. Okhsl holds "
                             "perceived lightness fixed. The gray strip under each is its lightness.");

                    ImGui::TextUnformatted("Wrong: HSV, s = 1, v = 1");
                    SweepWithLightness(frame * 1.5f, [](float t) -> OkLab
                    {
                        float r{ 0.0f };
                        float g{ 0.0f };
                        float b{ 0.0f };
                        ImGui::ColorConvertHSVtoRGB(t, 1.0f, 1.0f, r, g, b);
                        return LinearSrgbToOkLab(EncodedSrgbToLinearSrgb({ r, g, b }));
                    });

                    ImGui::TextUnformatted("Right: Okhsl, s = 1, l = 0.65");
                    SweepWithLightness(frame * 1.5f, [](float t) -> OkLab
                    {
                        return OkhslToOkLab({ t * 360.0f, 1.0f, 0.65f });
                    });
                }

                {
                    static float baseEncoded[3]{ 1.0f, 0.5f, 0.1f };
                    constexpr int STEP_COUNT{ 4 };
                    constexpr float RGB_SCALES[STEP_COUNT]{ 1.0f, 1.4f, 2.0f, 2.8f };
                    constexpr float LIGHTNESS_STEPS[STEP_COUNT]{ 0.0f, 0.06f, 0.12f, 0.18f };

                    Practice("Lighter or darker variants: OkLCh, change L only",
                             "Hover states, shades, tints. Scaling RGB clips the largest channel first, so the "
                             "others catch up and hue and saturation drift (orange turns yellow). Changing only "
                             "OkLCh's L keeps the hue.");

                    ImGui::PushItemWidth(editWidth);
                    ColorEdit3("Base", baseEncoded, ImOkStoredAs_EncodedSrgb);
                    ImGui::PopItemWidth();

                    const LinearSrgb baseLinear{ EncodedSrgbToLinearSrgb({ baseEncoded[0], baseEncoded[1], baseEncoded[2] }) };
                    const OkLCh baseLCh{ OkLabToOkLCh(LinearSrgbToOkLab(baseLinear)) };
                    OkLab scaled[STEP_COUNT]{};
                    OkLab lightened[STEP_COUNT]{};
                    for (int i{ 0 }; i < STEP_COUNT; ++i)
                    {
                        const float s{ RGB_SCALES[i] };
                        scaled[i] = LinearSrgbToOkLab({ std::fmin(baseLinear.r * s, 1.0f),
                                                        std::fmin(baseLinear.g * s, 1.0f),
                                                        std::fmin(baseLinear.b * s, 1.0f) });

                        lightened[i] = OkLChToOkLab({ baseLCh.L + LIGHTNESS_STEPS[i], baseLCh.C, baseLCh.h });
                    }

                    ImGui::TextUnformatted("Wrong: linear sRGB x 1, 1.4, 2, 2.8");
                    SwatchesWithLightness(scaled, STEP_COUNT);
                    ImGui::TextUnformatted("Right: OkLCh L + 0, 0.06, 0.12, 0.18");
                    SwatchesWithLightness(lightened, STEP_COUNT);
                }

                {
                    constexpr int SET_SIZE{ 6 };

                    Practice("Sets that must look equally light: Okhsl, fixed l",
                             "Team colors, chart series, UI accents. Equal HSV s and v only look equal on paper. "
                             "The grays show each color's Oklab L, how light it looks. RelativeLuminance is "
                             "physical brightness, for exposure and contrast ratios.");

                    OkLab hsvSet[SET_SIZE]{};
                    OkLab okhslSet[SET_SIZE]{};
                    for (int i{ 0 }; i < SET_SIZE; ++i)
                    {
                        const float t{ static_cast<float>(i) / SET_SIZE };
                        float r{ 0.0f };
                        float g{ 0.0f };
                        float b{ 0.0f };
                        ImGui::ColorConvertHSVtoRGB(t, 0.75f, 0.9f, r, g, b);
                        hsvSet[i] = LinearSrgbToOkLab(EncodedSrgbToLinearSrgb({ r, g, b }));
                        okhslSet[i] = OkhslToOkLab({ t * 360.0f, 0.75f, 0.65f });
                    }

                    ImGui::TextUnformatted("Wrong: HSV, s = 0.75, v = 0.9");
                    SwatchesWithLightness(hsvSet, SET_SIZE);
                    ImGui::TextUnformatted("Right: Okhsl, s = 0.75, l = 0.65");
                    SwatchesWithLightness(okhslSet, SET_SIZE);
                }

                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Leaving: encode once"))
            {
                Tldr("Tonemap with a curve, then encode exactly once. ImGui's colors are already encoded, "
                     "so ImGui draws through a UNORM view.");

                Practice("Tonemap with a curve, then encode",
                         "A lit scene goes past 1, and the screen stops there. Clamping turns every highlight "
                         "the same flat white; a curve rolls them off. Encoding comes after, as its own step.");
                ImGui::TextUnformatted("Light from 0 to 8 across the bar:");
                ImGui::TextUnformatted("Wrong: clamped at 1");
                GrayRamp(frame, 8.0f, [](float v) -> float { return EncodeChannel(std::fmin(v, 1.0f)); });
                ImGui::TextUnformatted("Right: a curve, x / (1 + x) (Reinhard)");
                GrayRamp(frame, 8.0f, [](float v) -> float { return EncodeChannel(v / (1.0f + v)); });

                Practice("Encode exactly once",
                         "Encode twice and everything is washed out. Skip it and everything is too dark.");
                ImGui::TextUnformatted("The same linear sRGB ramp, 0 to 1, three ways:");
                ImGui::TextUnformatted("Wrong: never encoded");
                GrayRamp(frame, 1.0f, [](float v) -> float { return v; });
                ImGui::TextUnformatted("Right: encoded once");
                GrayRamp(frame, 1.0f, [](float v) -> float { return EncodeChannel(v); });
                ImGui::TextUnformatted("Wrong: encoded twice");
                GrayRamp(frame, 1.0f, [](float v) -> float { return EncodeChannel(EncodeChannel(v)); });

                Practice("ImGui: through a UNORM view",
                         "ImGui's colors, ImOk's included, are already encoded. Through an _SRGB view they're "
                         "encoded again, and the UI washes out.");
                ImGui::TextWrapped("Your shader encodes: the swapchain stays UNORM, your last pass encodes, and "
                                   "ImGui draws after it. The example does this, with no scene before ImGui.");
                ImGui::TextWrapped("The swapchain encodes: draw the scene through an _SRGB view and ImGui through a "
                                   "UNORM view of the same buffer. DXGI's flip-model swapchains can't be _SRGB at "
                                   "all, so the buffer is UNORM and only the scene's view is _SRGB. In Vulkan the "
                                   "two views need VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR "
                                   "(VK_KHR_swapchain_mutable_format).");
                ImGui::TextUnformatted("This window's colors:");
                ImGui::TextUnformatted("Wrong: through an _SRGB view, encoded twice");
                StyleSwatches(true);
                ImGui::TextUnformatted("Right: through a UNORM view");
                StyleSwatches(false);

                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Picking: Okhsv or Okhsl"))
            {
                Tldr("Okhsv for general picking, Okhsl when lightness must hold. ImGui's HSV is neither.");

                Practice("Lightness must hold: Okhsl",
                         "Okhsv works like the HSV pickers artists know: use it for general picking. When "
                         "lightness must hold while the hue changes, use Okhsl, whose l is how light a color "
                         "looks. ImGui's HSV does neither well; 'Compared with ImGui' shows it side by side.");
                ImGui::TextUnformatted("Drag the hue bar on each:");
                {
                    static float okhsvColorEncoded[3]{ 0.8f, 0.3f, 0.2f };
                    static float okhslColorEncoded[3]{ 0.8f, 0.3f, 0.2f };
                    const float pickerWidth{ SideBySideWidth() };

                    ImGui::BeginGroup();
                    ImGui::PushID("okhsv");
                    ImGui::TextUnformatted("Wrong here: Okhsv, lightness jumps");
                    ImGui::PushItemWidth(pickerWidth);
                    ColorPicker3("picker", okhsvColorEncoded, ImOkStoredAs_EncodedSrgb, ImOkColorEditFlags_PickerOkhsv);
                    ImGui::PopItemWidth();
                    LightnessReadout(okhsvColorEncoded);
                    ImGui::PopID();
                    ImGui::EndGroup();

                    ImGui::SameLine();

                    ImGui::BeginGroup();
                    ImGui::PushID("okhsl");
                    ImGui::TextUnformatted("Right: Okhsl, lightness holds");
                    ImGui::PushItemWidth(pickerWidth);
                    ColorPicker3("picker", okhslColorEncoded, ImOkStoredAs_EncodedSrgb, ImOkColorEditFlags_PickerOkhsl);
                    ImGui::PopItemWidth();
                    LightnessReadout(okhslColorEncoded);
                    ImGui::PopID();
                    ImGui::EndGroup();
                }

                ImGui::TreePop();
            }

            ImGui::PopID();
        }

        if (ImGui::CollapsingHeader("1. Storage: what your floats hold", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::PushID("storage");
            Tldr("ImOkStoredAs says what your floats hold. It changes only how they're read and written; "
                 "the widget works the same either way. A wrong one gives a wrong color, with no error.");

            static float colorEncoded[3]{ 0.8f, 0.3f, 0.2f };
            static float colorLinear[3]{};
            static bool initialized{ false };
            if (!initialized)
            {
                EncodedFloatsToLinear(colorEncoded, colorLinear);
                initialized = true;
            }

            ImGui::TextWrapped("One color in two arrays: one stored as encoded sRGB, as ImGui and 8-bit textures "
                               "do, one as linear sRGB, as engines keep material and light colors. Edit either; "
                               "the other follows. Same color, different numbers.");

            ImGui::PushItemWidth(editWidth);
            if (ColorEdit3("Stored as encoded sRGB", colorEncoded, ImOkStoredAs_EncodedSrgb, ImOkColorEditFlags_DisplayRgb))
            {
                EncodedFloatsToLinear(colorEncoded, colorLinear);
            }
            if (ColorEdit3("Stored as linear sRGB", colorLinear, ImOkStoredAs_LinearSrgb, ImOkColorEditFlags_DisplayRgb))
            {
                LinearFloatsToEncoded(colorLinear, colorEncoded);
            }
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::TextWrapped("Mislabeled: the same floats read with the wrong ImOkStoredAs. Nothing fails; "
                               "the color is just wrong:");
            const ImVec2 swatchSize{ frame * 2.0f, frame };
            const EncodedSrgb reEncoded{ LinearSrgbToEncodedSrgb({ colorEncoded[0], colorEncoded[1], colorEncoded[2] }) };

            ImGui::ColorButton("##correct", ImVec4(colorEncoded[0], colorEncoded[1], colorEncoded[2], 1.0f),
                               ImGuiColorEditFlags_NoTooltip, swatchSize);
            ImGui::SameLine();
            ImGui::TextUnformatted("Right");

            ImGui::ColorButton("##linearAsEncoded", ImVec4(colorLinear[0], colorLinear[1], colorLinear[2], 1.0f),
                               ImGuiColorEditFlags_NoTooltip, swatchSize);
            ImGui::SameLine();
            ImGui::TextUnformatted("Linear sRGB floats read as encoded: too dark");

            ImGui::ColorButton("##encodedAsLinear", ImVec4(reEncoded.r, reEncoded.g, reEncoded.b, 1.0f),
                               ImGuiColorEditFlags_NoTooltip, swatchSize);
            ImGui::SameLine();
            ImGui::TextUnformatted("Encoded floats read as linear sRGB: washed out");

            ImGui::PopID();
        }

        if (ImGui::CollapsingHeader("2. Picker: how the artist moves through colors", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::PushID("picker");
            Tldr("The picker changes how the artist moves through colors. It never changes what's stored, "
                 "so any picker works with any storage.");

            static float colorEncoded[3]{ 0.8f, 0.3f, 0.2f };

            ImGui::TextWrapped("One encoded sRGB array, two pickers. Drag either; the other follows, since both "
                               "edit the same color. Only the square's layout differs. Which one to use when: "
                               "Good practices, Picking.");

            const float pickerWidth{ SideBySideWidth() };

            ImGui::BeginGroup();
            ImGui::TextUnformatted("Okhsv: saturation x value");
            ImGui::PushItemWidth(pickerWidth);
            ColorPicker3("Okhsv picker", colorEncoded, ImOkStoredAs_EncodedSrgb, ImOkColorEditFlags_PickerOkhsv);
            ImGui::PopItemWidth();
            ImGui::EndGroup();

            ImGui::SameLine();

            ImGui::BeginGroup();
            ImGui::TextUnformatted("Okhsl: saturation x lightness");
            ImGui::PushItemWidth(pickerWidth);
            ColorPicker3("Okhsl picker", colorEncoded, ImOkStoredAs_EncodedSrgb, ImOkColorEditFlags_PickerOkhsl);
            ImGui::PopItemWidth();
            ImGui::EndGroup();

            ImGui::PopID();
        }

        if (ImGui::CollapsingHeader("3. Display: which numbers the fields show", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::PushID("display");
            Tldr("The fields are only a view. Any display reads any storage, and switching never changes "
                 "your data.");

            static float colorEncoded[3]{ 0.2f, 0.5f, 0.8f };

            ImGui::TextWrapped("One encoded sRGB array, read five ways. Edit any row; the others follow. "
                               "Right-click a row to switch what it shows, or to copy the color, each format "
                               "named by its space. Hover a swatch for its values.");

            ImGui::PushItemWidth(editWidth);
            ColorEdit3("Okhsv", colorEncoded, ImOkStoredAs_EncodedSrgb, ImOkColorEditFlags_DisplayOkhsv);
            ColorEdit3("Okhsl", colorEncoded, ImOkStoredAs_EncodedSrgb, ImOkColorEditFlags_DisplayOkhsl);
            ColorEdit3("RGB (the floats as stored)", colorEncoded, ImOkStoredAs_EncodedSrgb, ImOkColorEditFlags_DisplayRgb);
            ColorEdit3("Hex (always encoded sRGB)", colorEncoded, ImOkStoredAs_EncodedSrgb, ImOkColorEditFlags_DisplayHex);
            ColorEdit3("OkLCh (read-only)", colorEncoded, ImOkStoredAs_EncodedSrgb, ImOkColorEditFlags_DisplayOkLCh);
            ImGui::PopItemWidth();

            ImGui::PopID();
        }

        if (ImGui::CollapsingHeader("4. Alpha: stored as-is"))
        {
            ImGui::PushID("alpha");
            Tldr("Alpha is stored as-is, whatever ImOkStoredAs says. It's coverage, so there is nothing "
                 "to encode.");

            static float colorEncoded[4]{ 0.2f, 0.5f, 0.8f, 0.5f };
            static float colorLinear[4]{};
            static bool initialized{ false };
            if (!initialized)
            {
                EncodedFloatsToLinear(colorEncoded, colorLinear);
                colorLinear[3] = colorEncoded[3];
                initialized = true;
            }

            ImGui::TextWrapped("One translucent color in two arrays, as in section 1. The RGB numbers "
                               "differ; A is the same number in both.");

            ImGui::PushItemWidth(editWidth);
            if (ColorEdit4("Stored as encoded sRGB", colorEncoded, ImOkStoredAs_EncodedSrgb, ImOkColorEditFlags_DisplayRgb))
            {
                EncodedFloatsToLinear(colorEncoded, colorLinear);
                colorLinear[3] = colorEncoded[3];
            }
            if (ColorEdit4("Stored as linear sRGB", colorLinear, ImOkStoredAs_LinearSrgb, ImOkColorEditFlags_DisplayRgb))
            {
                LinearFloatsToEncoded(colorLinear, colorEncoded);
                colorEncoded[3] = colorLinear[3];
            }
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::TextWrapped("Make a gray: drag to the square's left edge, then pick a hue on the bar. Now "
                               "fade alpha from code. The hue stays, since a change to alpha alone leaves the "
                               "picker as it was.");

            static float fadedEncoded[4]{ 0.5f, 0.5f, 0.5f, 1.0f };
            static bool fade{ false };
            ImGui::Checkbox("Fade alpha from code", &fade);
            if (fade)
            {
                fadedEncoded[3] = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 2.0f);
            }

            ImGui::PushItemWidth(SideBySideWidth());
            ColorPicker4("Faded", fadedEncoded, ImOkStoredAs_EncodedSrgb);
            ImGui::PopItemWidth();

            ImGui::PopID();
        }

        if (ImGui::CollapsingHeader("5. Native perceptual widgets"))
        {
            ImGui::PushID("native");
            Tldr("When your tool works in Okhsv or Okhsl, keep that value and convert to linear sRGB only "
                 "where the renderer needs it.");

            static Okhsv okhsv{ 30.0f, 0.8f, 0.7f };
            static Okhsl okhsl{ 200.0f, 0.8f, 0.6f };
            static float okhsvAlpha{ 0.75f };

            ImGui::TextWrapped("For tools that work in perceptual values: palettes, generated colors. There is no "
                               "storage choice; the value is the color, so hue and saturation are never lost at "
                               "gray or black, even when set from code.");
            ImGui::TextWrapped("Keep Okhsv or Okhsl inside the tool. As asset data, store Oklab, OkLCh or linear "
                               "sRGB: Okhsv and Okhsl are coordinates in the sRGB gamut, and mean something else "
                               "if you later target a wider one.");
            ImGui::TextWrapped("Alpha is optional. The Okhsv picker gets a pointer to it and the Okhsl picker "
                               "doesn't, so the Okhsv square is one bar narrower. The item width covers the whole "
                               "picker, bars included, as in ImGui.");

            const float pickerWidth{ SideBySideWidth() };

            ImGui::BeginGroup();
            ImGui::PushID("okhsv");
            ImGui::TextUnformatted("ColorPickerOkhsv, with alpha");
            ImGui::PushItemWidth(pickerWidth);
            ColorPickerOkhsv("##picker", &okhsv, &okhsvAlpha);
            ImGui::PopItemWidth();
            ImGui::Text("h %.1f s %.3f v %.3f a %.3f", okhsv.h, okhsv.s, okhsv.v, okhsvAlpha);
            ResultSwatch(OkhsvToOkLab(okhsv), okhsvAlpha);
            ImGui::PopID();
            ImGui::EndGroup();

            ImGui::SameLine();

            ImGui::BeginGroup();
            ImGui::PushID("okhsl");
            ImGui::TextUnformatted("ColorPickerOkhsl");
            ImGui::PushItemWidth(pickerWidth);
            ColorPickerOkhsl("##picker", &okhsl);
            ImGui::PopItemWidth();
            ImGui::Text("h %.1f  s %.3f  l %.3f", okhsl.h, okhsl.s, okhsl.l);
            ResultSwatch(OkhslToOkLab(okhsl));
            ImGui::PopID();
            ImGui::EndGroup();

            ImGui::PopID();
        }

        if (ImGui::CollapsingHeader("6. Lights and emission: color, temperature, intensity"))
        {
            ImGui::PushID("lights");
            Tldr("A light is three values your engine multiplies: a color with no brightness, a temperature, "
                 "and an intensity in a unit you name. Nothing ImOk stores goes above 1; brightness is only "
                 "ever the intensity.");

            const ImVec2 swatchSize{ frame * 2.0f, frame };
            const float lightWidth{ -ImGui::GetFontSize() * 8.0f };
            ImVec2 pMin{};
            ImVec2 pMax{};

            static float lampColorLinear[3]{ 1.0f, 1.0f, 1.0f };
            static float lampKelvin{ 2700.0f };
            static float lampLumens{ 800.0f };
            static float spotColorLinear[3]{ 0.2f, 0.4f, 1.0f };
            static float spotKelvin{ 3200.0f };
            static float spotLumens{ 800.0f };
            static float sunColorLinear[3]{ 1.0f, 1.0f, 1.0f };
            static float sunKelvin{ 5500.0f };
            static float sunLux{ 56569.0f };
            static float panelColorEncoded[3]{ 1.0f, 1.0f, 1.0f };
            static float panelKelvin{ 4000.0f };
            static float panelNits{ 255.0f };
            static float signColorLinear[3]{ 1.0f, 0.1f, 0.3f };
            static float signNits{ 203.0f };
            static float exposureEv{ 12.0f };
            static const char* lastCommit{ "nothing yet" };

            // imok.h's undo pattern
            const auto commitIfDone{ [](bool changed, const char* name)
            {
                if (ImGui::IsItemDeactivatedAfterEdit() || (changed && !ImGui::IsItemActive()))
                {
                    lastCommit = name;
                }
            } };

            ImGui::SeparatorText("One row per light");
            ImGui::TextWrapped("One light per kind. Click a swatch for the color and temperature; drag the bar "
                               "for the intensity, and hover it for what the unit means for that kind. The "
                               "swatch shows the light's color without its brightness.");

            ImGui::PushItemWidth(lightWidth);
            commitIfDone(LightEdit("Desk lamp", ImOkLightKind_Point, lampColorLinear, ImOkStoredAs_LinearSrgb,
                                   &lampKelvin, &lampLumens, ImOkLightUnit_Lumen), "Desk lamp");
            commitIfDone(LightEdit("Stage spot, blue gel", ImOkLightKind_Spot, spotColorLinear, ImOkStoredAs_LinearSrgb,
                                   &spotKelvin, &spotLumens, ImOkLightUnit_Lumen), "Stage spot");
            commitIfDone(LightEdit("Sun", ImOkLightKind_Directional, sunColorLinear, ImOkStoredAs_LinearSrgb,
                                   &sunKelvin, &sunLux, ImOkLightUnit_Lux), "Sun");
            commitIfDone(LightEdit("Ceiling panel", ImOkLightKind_Area, panelColorEncoded, ImOkStoredAs_EncodedSrgb,
                                   &panelKelvin, &panelNits, ImOkLightUnit_Nits), "Ceiling panel");
            commitIfDone(LightEdit("Neon sign", ImOkLightKind_Emissive, signColorLinear, ImOkStoredAs_LinearSrgb,
                                   nullptr, &signNits, ImOkLightUnit_Nits), "Neon sign");
            ImGui::PopItemWidth();

            ImGui::Text("Last undo step: %s", lastCommit);
            ImGui::TextWrapped("Each row is one item: a drag on its bar or anywhere in its popup is one undo "
                               "step, with all three values in it. Dropping a color on a swatch sets the "
                               "color, a filter over the temperature. The ceiling panel stores its color as "
                               "encoded sRGB, as 8-bit light colors usually are; the widget works the same.");
            ImGui::TextWrapped("The neon sign is emission: a glowing surface, usually in nits. Neon isn't a "
                               "blackbody, so its kelvin is nullptr. For any light with its temperature off, pass "
                               "nullptr: 6500 K is faintly pink. Hover its bar for references, such as HDR "
                               "reference white at 203 nits.");

            ImGui::SeparatorText("Inside a row: three values");
            ImGui::TextWrapped("The desk lamp, taken apart. The color is picked without brightness "
                               "(ImOkColorEditFlags_NoBrightness: hue and saturation only, the largest channel "
                               "always 1). The temperature is the lamp. The intensity is how bright, here in "
                               "lumens. Your engine multiplies them, as pbrt-v4 does:");

            ImGui::PushItemWidth(lightWidth);
            ColorEdit3("Color", lampColorLinear, ImOkStoredAs_LinearSrgb, ImOkColorEditFlags_NoBrightness);
            TemperatureEdit("Temperature", &lampKelvin);
            IntensityEdit("Intensity", &lampLumens, ImOkLightUnit_Lumen);
            ImGui::PopItemWidth();

            ImGui::TextUnformatted("light = color x temperature / luminance(temperature) x intensity");
            const LinearSrgb temperature{ KelvinToLinearSrgb(lampKelvin) };
            const LinearSrgb filtered{ lampColorLinear[0] * temperature.r, lampColorLinear[1] * temperature.g,
                                       lampColorLinear[2] * temperature.b };
            const float scale{ lampLumens / RelativeLuminance(temperature) };
            const LinearSrgb light{ filtered.r * scale, filtered.g * scale, filtered.b * scale };

            const float largest{ std::fmax(std::fmax(filtered.r, filtered.g), filtered.b) };
            const LinearSrgb shown{ (largest > 0.0f) ? LinearSrgb{ filtered.r / largest, filtered.g / largest,
                                                                   filtered.b / largest }
                                                     : LinearSrgb{} };
            ImGui::ColorButton("##lamp", LinearSrgbToImVec4(shown), ImGuiColorEditFlags_NoTooltip, swatchSize);
            ImGui::SameLine();
            ImGui::TextUnformatted("The lamp's color, without its brightness");
            ImGui::Text("Result, before your engine's own scale: %.1f %.1f %.1f (luminance %.1f)",
                        light.r, light.g, light.b, RelativeLuminance(light));
            ImGui::TextWrapped("The result goes far above 1: that product is where HDR comes from. The three "
                               "values stay in their own ranges.");
            ImGui::TextWrapped("With a white color the luminance is exactly the intensity, at any temperature: "
                               "the temperature only colors the light. A colored filter removes light, like a "
                               "gel over the lamp.");
            ImGui::TextWrapped("Start with a white color and set the temperature: most lamps get their color "
                               "from what they are, and the bar's ticks show common ones (hover it). Change "
                               "the color when there is a reason: stylized light, a colored gel, or a source "
                               "that is not a blackbody, such as neon or colored LEDs.");

            ImGui::SeparatorText("The color is a filter");
            ImGui::TextWrapped("Hue alone changes how much light passes. Under each fully saturated hue, its "
                               "relative luminance as a gray: blue passes 7%% of white's light, yellow 93%%. At "
                               "the same intensity a blue light is dim, as a blue gel would make it. To keep "
                               "the intensity for any color, divide by the luminance of color x temperature "
                               "instead.");
            SweepWithLuminance(frame * 1.5f, [](float t) -> LinearSrgb
            {
                return OkLabToLinearSrgb(OkhsvToOkLab({ t * 360.0f, 1.0f, 1.0f }));
            }, pMin, pMax);

            ImGui::SeparatorText("Temperature is the lamp");
            ImGui::TextWrapped("KelvinToLinearSrgb gives a blackbody's color, as pbrt-v4 computes it: physics, "
                               "with no white point. No temperature is white, because D65 lies off the "
                               "blackbody curve: 6500 K is faintly pink. White balance belongs to the camera. "
                               "Warm light also has less luminance at its largest channel 1, which is why the "
                               "formula divides by it.");
            ImGui::ColorButton("##6500", LinearSrgbToImVec4(KelvinToLinearSrgb(6500.0f)), ImGuiColorEditFlags_NoTooltip, swatchSize);
            ImGui::SameLine();
            ImGui::TextUnformatted("6500 K");
            ImGui::SameLine();
            ImGui::ColorButton("##white", LinearSrgbToImVec4({ 1.0f, 1.0f, 1.0f }), ImGuiColorEditFlags_NoTooltip, swatchSize);
            ImGui::SameLine();
            ImGui::TextUnformatted("White (D65)");

            ImGui::SeparatorText("Units");
            ImGui::TextWrapped("ImOkLightUnit is required, like ImOkStoredAs: the same 800 is a bulb in lumens "
                               "and a dim room in lux. Lumens and lux are pbrt-v4's quantities, for local and "
                               "distant lights; candela, nits and EV100 are how Unreal and HDRP express the same "
                               "light. ImOk labels and ranges the number and never converts it: what 1 means in "
                               "your shader is your engine's choice.");
            ImGui::TextWrapped("Lumens are the light a source emits in total, the number on a bulb's box. "
                               "Candela is light per direction and nits light per m2 of surface: what a shader "
                               "needs, so an engine storing lumens divides by the light's shape. Choosing the "
                               "unit chooses what an artist keeps when resizing a light. At 800 lm, a spot "
                               "narrowed from 45 to 20 degrees goes from 435 to 2111 cd (Unreal's formula): "
                               "the same light, concentrated. A 1 m2 panel at 800 lm is 255 nits, 2 m2 is "
                               "127: dimmer to look at, the same light in the room. In candela or nits, "
                               "resizing keeps the look and changes the light instead.");
            ImGui::TextWrapped("Each kind takes only the units that fit it, and the unit is set in code. "
                               "Switching it without converting would change every light's brightness, and "
                               "converting needs the light's shape, which ImOk doesn't have.");
            ImGui::TextWrapped("IntensityEdit on its own takes any unit, exposure included:");
            ImGui::PushItemWidth(lightWidth);
            IntensityEdit("Exposure", &exposureEv, ImOkLightUnit_EV100);
            ImGui::PopItemWidth();

            ImGui::SeparatorText("Spot lights: engines disagree");
            ImGui::TextWrapped("The stage spot's lumens in candela, by each engine's formula. Angles are "
                               "measured from the axis. pbrt-v4 counts the falloff between the inner and "
                               "outer cone at half; Unreal takes the outer cone only; HDRP does the same with "
                               "Reflector on, and with it off spreads the lumens over the whole sphere, like a "
                               "point light.");
            static float spotInner{ 30.0f };
            static float spotOuter{ 45.0f };
            ImGui::PushItemWidth(editWidth * 0.5f);
            ImGui::SliderFloat("Inner cone", &spotInner, 0.0f, spotOuter, "%.0f deg");
            ImGui::SliderFloat("Outer cone", &spotOuter, spotInner, 89.0f, "%.0f deg");
            ImGui::PopItemWidth();
            {
                constexpr float PI{ 3.14159265f };
                const float cosInner{ std::cos(spotInner * PI / 180.0f) };
                const float cosOuter{ std::cos(spotOuter * PI / 180.0f) };
                const float pbrtSteradians{ 2.0f * PI * ((1.0f - cosInner) + (cosInner - cosOuter) * 0.5f) };
                const float coneSteradians{ 2.0f * PI * (1.0f - cosOuter) };
                const float sphereSteradians{ 4.0f * PI };
                ImGui::BulletText("pbrt-v4: %.0f cd", spotLumens / pbrtSteradians);
                ImGui::BulletText("Unreal, and HDRP with Reflector: %.0f cd", spotLumens / coneSteradians);
                ImGui::BulletText("HDRP without Reflector: %.0f cd", spotLumens / sphereSteradians);
            }
            ImGui::TextWrapped("Moving a scene between engines in lumens moves these differences with it. In "
                               "candela it doesn't, but then the cone no longer changes the brightness.");

            ImGui::SeparatorText("LightPicker: the popup, inline");
            ImGui::TextWrapped("The desk lamp again, as LightPicker. The line at the bottom is the intensity "
                               "that passes the color, in the same unit. Pick a saturated blue: over 2700 K "
                               "it passes about 1.4%%, since a warm lamp has little blue to pass.");
            ImGui::PushItemWidth(frame * 14.0f);
            commitIfDone(LightPicker("##lamp picker", ImOkLightKind_Point, lampColorLinear, ImOkStoredAs_LinearSrgb,
                                     &lampKelvin, &lampLumens, ImOkLightUnit_Lumen), "Desk lamp");
            ImGui::PopItemWidth();

            ImGui::SeparatorText("Faders");
            ImGui::TextWrapped("VTemperatureEdit and VIntensityEdit: the same widgets as vertical faders, for "
                               "panels of several lights. These two edit the desk lamp too. Size them with an "
                               "ImVec2, 0 on an axis for a default.");
            VTemperatureEdit("##lamp temperature", ImVec2(0.0f, 0.0f), &lampKelvin);
            ImGui::SameLine();
            VIntensityEdit("##lamp intensity", ImVec2(0.0f, 0.0f), &lampLumens, ImOkLightUnit_Lumen);

            ImGui::PopID();
        }

        if (ImGui::CollapsingHeader("7. Drag and drop: same color, new numbers"))
        {
            ImGui::PushID("dragdrop");
            Tldr("Drag any swatch onto any color widget, ImGui's included. Each widget converts the "
                 "color to its own storage, so the numbers differ and the color doesn't.");

            static float imokEncoded[3]{ 0.8f, 0.3f, 0.2f };
            static float imokLinear[3]{ 0.02f, 0.2f, 0.6f };
            static float imguiEncoded[3]{ 0.3f, 0.7f, 0.3f };
            static float imokLinearAlpha[4]{ 0.8f, 0.5f, 0.02f, 0.5f };
            static float imguiEncodedAlpha[4]{ 0.6f, 0.2f, 0.8f, 1.0f };

            ImGui::TextWrapped("Drag one swatch onto another widget. ImGui's widgets take part too: their color "
                               "payloads are encoded sRGB floats, and ImOk sends the same.");

            ImGui::PushItemWidth(editWidth);
            ColorEdit3("ImOk, stored as encoded sRGB", imokEncoded, ImOkStoredAs_EncodedSrgb, ImOkColorEditFlags_DisplayRgb);
            ColorEdit3("ImOk, stored as linear sRGB", imokLinear, ImOkStoredAs_LinearSrgb, ImOkColorEditFlags_DisplayRgb);
            ImGui::ColorEdit3("ImGui::ColorEdit3", imguiEncoded, ImGuiColorEditFlags_Float);
            ColorEdit4("ImOk with alpha, stored as linear sRGB", imokLinearAlpha, ImOkStoredAs_LinearSrgb, ImOkColorEditFlags_DisplayRgb);
            ImGui::ColorEdit4("ImGui::ColorEdit4", imguiEncodedAlpha, ImGuiColorEditFlags_Float);
            ImGui::PopItemWidth();

            ImGui::TextWrapped("A 3-variant dropped on a 4-variant leaves alpha as it was; a 4-variant brings "
                               "its alpha along.");

            // Encoded sRGB even from the linear sRGB rows, at full precision
            const ImGuiPayload* payload{ ImGui::GetDragDropPayload() };
            const bool color3{ payload != nullptr && payload->IsDataType(IMGUI_PAYLOAD_TYPE_COLOR_3F)
                               && payload->DataSize >= static_cast<int>(sizeof(float) * 3) };
            const bool color4{ payload != nullptr && payload->IsDataType(IMGUI_PAYLOAD_TYPE_COLOR_4F)
                               && payload->DataSize >= static_cast<int>(sizeof(float) * 4) };
            if (color3 || color4)
            {
                float sent[4]{};
                std::memcpy(sent, payload->Data, sizeof(float) * (color4 ? 4 : 3));
                if (color4)
                {
                    ImGui::Text("Payload %s: %.9g %.9g %.9g, alpha %.9g", payload->DataType, sent[0], sent[1], sent[2], sent[3]);
                }
                else
                {
                    ImGui::Text("Payload %s: %.9g %.9g %.9g", payload->DataType, sent[0], sent[1], sent[2]);
                }
            }
            else
            {
                ImGui::TextDisabled("Payload: drag a swatch to see what it sends");
            }

            ImGui::SeparatorText("Floats that are really linear sRGB");

            static float linearInImGui[3]{};
            static bool initialized{ false };
            if (!initialized)
            {
                EncodedFloatsToLinear(imokEncoded, linearInImGui);
                initialized = true;
            }

            ImGui::TextWrapped("ImGui's color payloads carry no color space: they are floats that ImGui, and "
                               "ImOk, read as encoded sRGB. This ImGui widget holds linear sRGB floats, so it shows "
                               "the orange too dark, and every widget it is dropped on receives that darker "
                               "color. Nothing can detect this; only naming the space prevents it (see "
                               "section 1).");

            ImGui::PushItemWidth(editWidth);
            ImGui::ColorEdit3("ImGui::ColorEdit3, holding linear floats", linearInImGui, ImGuiColorEditFlags_Float);
            ImGui::PopItemWidth();

            // No drag source: this swatch is a display, not data
            const ImVec2 swatchSize{ frame * 2.0f, frame };
            const LinearSrgb meant{ linearInImGui[0], linearInImGui[1], linearInImGui[2] };
            ImGui::ColorButton("##meant", LinearSrgbToImVec4(meant),
                               ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, swatchSize);
            ImGui::SameLine();
            ImGui::TextUnformatted("What those floats mean, read as linear sRGB");

            ImGui::SeparatorText("A drop keeps what it doesn't define");

            static Okhsl native{ 150.0f, 0.9f, 0.6f };
            static float storedEncoded[3]{ 0.2f, 0.7f, 0.45f };

            ImGui::TextWrapped("Drop the gray on either picker: the hue stays and saturation goes to 0. Drop "
                               "black or white: hue and saturation both stay, so dragging lightness back brings "
                               "the color back. Typed hex or RGB and the popup's Original work the same way. "
                               "Only a change from outside (code, undo, another widget) starts over from the "
                               "new color.");

            // ImGui swatches as drag sources; NoAlpha sends 3 floats
            const char* const names[]{ "Gray", "Black", "White" };
            const float values[]{ 0.5f, 0.0f, 1.0f };
            for (int i{ 0 }; i < 3; ++i)
            {
                ImGui::ColorButton(names[i], ImVec4(values[i], values[i], values[i], 1.0f),
                                   ImGuiColorEditFlags_NoAlpha, swatchSize);
                ImGui::SameLine();
                ImGui::TextUnformatted(names[i]);
                if (i < 2)
                {
                    ImGui::SameLine();
                }
            }

            const float pickerWidth{ SideBySideWidth() };

            ImGui::BeginGroup();
            ImGui::TextUnformatted("ColorPickerOkhsl: the value");
            ImGui::PushItemWidth(pickerWidth);
            ColorPickerOkhsl("##native", &native);
            ImGui::PopItemWidth();
            ImGui::Text("h %.1f  s %.3f  l %.3f", native.h, native.s, native.l);
            ImGui::EndGroup();

            ImGui::SameLine();

            ImGui::BeginGroup();
            ImGui::TextUnformatted("ColorPicker3: encoded floats");
            ImGui::PushItemWidth(pickerWidth);
            ColorPicker3("##stored", storedEncoded, ImOkStoredAs_EncodedSrgb, ImOkColorEditFlags_PickerOkhsl);
            ImGui::PopItemWidth();
            ImGui::Text("col %.3f %.3f %.3f", storedEncoded[0], storedEncoded[1], storedEncoded[2]);
            ImGui::EndGroup();

            ImGui::PopID();
        }

        if (ImGui::CollapsingHeader("8. Compared with ImGui's picker"))
        {
            ImGui::PushID("imgui");
            Tldr("One array, two pickers. ImOk's moves by how colors look; ImGui's by the encoded numbers.");

            static float colorEncoded[3]{ 0.8f, 0.3f, 0.2f };

            ImGui::TextWrapped("The same encoded sRGB array, edited by both. ImGui's HSV is computed from the "
                               "encoded numbers: its hue steps are uneven, and neither S nor V follows what you "
                               "see. Okhsv evens out hue and saturation. For equal lightness across hues, "
                               "right-click ImOk's picker, switch to Okhsl and drag the hue; then drag H on "
                               "ImGui's side.");

            const float pickerWidth{ SideBySideWidth() };

            ImGui::BeginGroup();
            ImGui::TextUnformatted("ImOk::ColorPicker3");
            ImGui::PushItemWidth(pickerWidth);
            ColorPicker3("ImOk picker", colorEncoded, ImOkStoredAs_EncodedSrgb);
            ColorEdit3("##ImOk fields", colorEncoded, ImOkStoredAs_EncodedSrgb);
            ImGui::PopItemWidth();
            ImGui::EndGroup();

            ImGui::SameLine();

            ImGui::BeginGroup();
            ImGui::TextUnformatted("ImGui::ColorPicker3");
            ImGui::PushItemWidth(pickerWidth);
            ImGui::ColorPicker3("##ImGui picker", colorEncoded,
                                ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_NoSidePreview);
            ImGui::PopItemWidth();
            ImGui::EndGroup();

            ImGui::PopID();
        }

        ImGui::End();
    }
}