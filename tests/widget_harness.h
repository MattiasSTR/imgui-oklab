// Real Dear ImGui frames with no backend and a simulated mouse; nothing is rendered. Positions
// inside ImOk widgets are computed from their layout, marked MIRRORS: if a layout changes, the
// tests fail loudly, never pass by accident.

#pragma once

#include "imgui.h"
// Tests may read ImGui's internals to find things on screen: popups, tooltips, window sizes
#include "imgui_internal.h"
#include <cmath>
#include <cstring>
#include <string>

namespace ImOkTest
{
    // Item width the tests push for every widget, so layout positions can be computed
    constexpr float ITEM_WIDTH{ 300.0f };

    struct ItemRect
    {
        ImVec2 min{};
        ImVec2 max{};

        ImVec2 Center() const { return ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f); }
        // Inside the item, near its left edge: on its first field or on its square
        ImVec2 Left() const { return ImVec2(min.x + 10.0f, (min.y + max.y) * 0.5f); }
    };

    inline ItemRect LastItemRect()
    {
        return { ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };
    }

    // MIRRORS imok.cpp ColorEditImpl (and ImGui's own ColorEdit4): the swatch is the
    // frame-height square that ends the item width
    inline ImVec2 SwatchCenter(const ItemRect& edit)
    {
        const float frame{ ImGui::GetFrameHeight() };
        return ImVec2(edit.min.x + ITEM_WIDTH - frame * 0.5f, (edit.min.y + edit.max.y) * 0.5f);
    }

    // MIRRORS imok.cpp HueSquarePicker: a square, then frame-height bars (hue, then alpha),
    // all within the item width. A point on the hue bar at height t (0 top, 1 bottom).
    inline ImVec2 HueBarPoint(const ItemRect& picker, bool hasAlphaBar, float t)
    {
        const float bar{ ImGui::GetFrameHeight() };
        const float spacing{ ImGui::GetStyle().ItemInnerSpacing.x };
        const float square{ ITEM_WIDTH - (hasAlphaBar ? 2.0f : 1.0f) * (bar + spacing) };
        return ImVec2(picker.min.x + square + spacing + bar * 0.5f, picker.min.y + square * t);
    }

    // MIRRORS imok.cpp PickerPopupContent: a 12-frame picker, then the column of the Current and
    // Original swatches (3 frames wide), each below its text label. Its center's x.
    inline float ColorPopupColumnX(const ItemRect& popup)
    {
        const float frame{ ImGui::GetFrameHeight() };
        const ImGuiStyle& style{ ImGui::GetStyle() };
        return popup.min.x + style.WindowPadding.x + frame * 12.0f + style.ItemInnerSpacing.x + frame * 1.5f;
    }

    // MIRRORS imok.cpp LightEditImpl: a frame-height swatch first, then IntensityEdit's bar
    inline ImVec2 LightSwatchCenter(const ItemRect& edit)
    {
        return ImVec2(edit.min.x + ImGui::GetFrameHeight() * 0.5f, (edit.min.y + edit.max.y) * 0.5f);
    }

    // A point on LightEdit's intensity bar, dx from its left end
    inline ImVec2 LightBarPoint(const ItemRect& edit, float dx)
    {
        const float start{ edit.min.x + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x };
        return ImVec2(start + dx, (edit.min.y + edit.max.y) * 0.5f);
    }

    // MIRRORS imok.cpp LightPickerBody in LightEdit's popup (LIGHT_POPUP_WIDTH_FRAMES, 14): the
    // swatch column (Color, Light, Original), 2 frames wide, ends the width. Its center's x.
    inline float LightPopupColumnX(const ItemRect& popup)
    {
        return popup.min.x + ImGui::GetStyle().WindowPadding.x + ImGui::GetFrameHeight() * 13.0f;
    }

    // Where the test window lives. Docked and Viewports need ImGui's docking branch.
    enum class HarnessMode
    {
        Window,    // A plain fixed window at the top left
        Docked,    // Docked into a dockspace over the main viewport: inside a dock node's host
        Viewports, // Multi-viewports on a fake platform: windows that stick out get their own
    };

#ifdef IMOK_TEST_DOCKED
    constexpr HarnessMode DEFAULT_HARNESS_MODE{ HarnessMode::Docked };
#else
    constexpr HarnessMode DEFAULT_HARNESS_MODE{ HarnessMode::Window };
#endif

    // A color payload, as it was while being dragged
    struct Payload
    {
        char type[33]{};
        float data[4]{};
        int count{ 0 }; // Floats: 3 or 4, 0 if no color payload was seen
    };

    class Harness
    {
    public:
        explicit Harness(HarnessMode mode = DEFAULT_HARNESS_MODE)
        {
            ImGui::CreateContext();
            ImGuiIO& io{ ImGui::GetIO() };
            io.DisplaySize = ImVec2(1200.0f, 1200.0f);
            io.IniFilename = nullptr;
            // Ctrl, not Cmd, for Type()'s Ctrl+click, on every platform
            io.ConfigMacOSXBehaviors = false;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures; // Fonts build without a renderer
            // As most official backends: the demo with every section open is 106352 vertices
            io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

            // Kept here: ImGui's default on Windows is the OS clipboard, which a test run would overwrite
            ImGuiPlatformIO& platformIo{ ImGui::GetPlatformIO() };
            platformIo.Platform_ClipboardUserData = this;
            platformIo.Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text)
            {
                static_cast<Harness*>(ImGui::GetPlatformIO().Platform_ClipboardUserData)->m_clipboard = text;
            };
            platformIo.Platform_GetClipboardTextFn = [](ImGuiContext*) -> const char*
            {
                return static_cast<Harness*>(ImGui::GetPlatformIO().Platform_ClipboardUserData)->m_clipboard.c_str();
            };

#ifdef IMGUI_HAS_DOCK
            if (mode != HarnessMode::Window)
            {
                io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            }
            m_docked = (mode == HarnessMode::Docked);
            if (mode == HarnessMode::Viewports)
            {
                InstallFakePlatform();
            }
#else
            IM_ASSERT(mode == HarnessMode::Window && "Docked and Viewports need Dear ImGui's docking branch.");
            (void)mode; // Read only by the assert, which Release builds compile out
#endif
        }

        ~Harness()
        {
#ifdef IMGUI_HAS_VIEWPORT
            ImGui::DestroyPlatformWindows();
#endif
            ImGui::DestroyContext();
        }

        Harness(const Harness&) = delete;
        Harness& operator=(const Harness&) = delete;

        // One frame with the mouse at `mouse`, left button down or up (the right button follows
        // RightClick). ui() is called inside a fixed window at the top left, with ITEM_WIDTH pushed.
        template <typename Ui>
        void Frame(const Ui& ui, ImVec2 mouse, bool down)
        {
            ImGuiIO& io{ ImGui::GetIO() };
            io.AddMousePosEvent(mouse.x, mouse.y);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
            io.AddMouseButtonEvent(ImGuiMouseButton_Right, m_right);
            io.AddKeyEvent(ImGuiMod_Alt, m_alt);
            io.AddKeyEvent(ImGuiMod_Shift, m_shift);
            io.AddKeyEvent(ImGuiMod_Ctrl, m_ctrl);
            io.DeltaTime = 1.0f / 60.0f;

            ImGui::NewFrame();
#ifdef IMGUI_HAS_DOCK
            if (m_docked)
            {
                ImGui::SetNextWindowDockID(ImGui::DockSpaceOverViewport(), ImGuiCond_Always);
            }
            else
#endif
            {
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(ImVec2(800.0f, 1100.0f));
            }
            ImGui::Begin("Test", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            ImGui::PushItemWidth(ITEM_WIDTH);
            ui();
            ImGui::PopItemWidth();
            ImGui::End();

            if (const ImGuiPayload* payload{ ImGui::GetDragDropPayload() })
            {
                const bool color3{ payload->IsDataType(IMGUI_PAYLOAD_TYPE_COLOR_3F) };
                const bool color4{ payload->IsDataType(IMGUI_PAYLOAD_TYPE_COLOR_4F) };
                if (color3 || color4)
                {
                    // Same size as ImGui's field (char[32 + 1]), which ImGui always terminates:
                    // copy it whole, terminator included
                    static_assert(sizeof(m_payload.type) == sizeof(payload->DataType), "Payload::type mirrors ImGuiPayload::DataType");
                    std::memcpy(m_payload.type, payload->DataType, sizeof(m_payload.type));
                    m_payload.count = color4 ? 4 : 3;
                    std::memcpy(m_payload.data, payload->Data, sizeof(float) * static_cast<size_t>(m_payload.count));
                }
            }
            ImGui::Render();
#ifdef IMGUI_HAS_VIEWPORT
            if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
            {
                ImGui::UpdatePlatformWindows(); // Creates, moves and sizes the fake windows
            }
#endif
        }

        // Frames with the mouse still and up, so every widget has been laid out
        template <typename Ui>
        void Settle(const Ui& ui, int frames = 2)
        {
            for (int i{ 0 }; i < frames; ++i)
            {
                Frame(ui, ImVec2(-1.0f, -1.0f), false);
            }
        }

        // Press at `from`, move to `to` in steps (well past ImGui's drag threshold), release
        template <typename Ui>
        void Drag(const Ui& ui, ImVec2 from, ImVec2 to)
        {
            constexpr int STEPS{ 10 };
            m_payload = {};
            Frame(ui, from, false);
            Frame(ui, from, true);
            for (int i{ 1 }; i <= STEPS; ++i)
            {
                const float t{ static_cast<float>(i) / STEPS };
                Frame(ui, ImVec2(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t), true);
            }
            Frame(ui, to, true);
            Frame(ui, to, false); // Release: a drop is delivered here
            Frame(ui, to, false);
        }

        template <typename Ui>
        void Click(const Ui& ui, ImVec2 at)
        {
            Frame(ui, at, false);
            Frame(ui, at, true);
            Frame(ui, at, false);
            Frame(ui, at, false);
        }

        // Right-clicks at `at`: context menus open on the release (OpenPopupOnItemClick)
        template <typename Ui>
        void RightClick(const Ui& ui, ImVec2 at)
        {
            Frame(ui, at, false);
            m_right = true;
            Frame(ui, at, false);
            m_right = false;
            Frame(ui, at, false);
            Frame(ui, at, false);
        }

        // Holds the mouse still at `at` for `frames` frames: long enough for a tooltip
        // (style.HoverFlagsForTooltipMouse: stationary 0.15 s and a short delay 0.15 s by default,
        // 9 frames each at 60 Hz)
        template <typename Ui>
        void Hover(const Ui& ui, ImVec2 at, int frames = 30)
        {
            for (int i{ 0 }; i < frames; ++i)
            {
                Frame(ui, at, false);
            }
        }

        // Types text into the text field at `field`, replacing all of it, then presses Enter.
        // Ctrl+click activates a single-line InputText with its text selected (ImGui's
        // InputTextEx), so the characters replace it, all in one frame: one edit, as a paste.
        template <typename Ui>
        void Type(const Ui& ui, ImVec2 field, const char* text)
        {
            ImGuiIO& io{ ImGui::GetIO() };
            m_ctrl = true;
            Frame(ui, field, false);
            Frame(ui, field, true);
            m_ctrl = false;
            Frame(ui, field, false);
            io.AddInputCharactersUTF8(text);
            Frame(ui, field, false);
            io.AddKeyEvent(ImGuiKey_Enter, true);
            Frame(ui, field, false);
            io.AddKeyEvent(ImGuiKey_Enter, false);
            Frame(ui, field, false);
        }

        // Moves the mouse along a line in 2 px steps and returns, in `found`, a point inside each
        // hover region it passes, in order: 6 px past where it entered, not on the edge. Text has
        // no ID, so only widgets count. Touching items form one region (menu rows do). Items under
        // an open popup don't hover: ImGui blocks them.
        template <typename Ui>
        int FindHoverablesOnLine(const Ui& ui, ImVec2 from, ImVec2 to, ImVec2* found, int maxFound)
        {
            const float dx{ to.x - from.x };
            const float dy{ to.y - from.y };
            const float length{ std::sqrt(dx * dx + dy * dy) };
            if (length <= 0.0f)
            {
                return 0;
            }
            const ImVec2 direction{ dx / length, dy / length };
            int count{ 0 };
            bool wasHovered{ false };
            for (float t{ 0.0f }; t < length && count < maxFound; t += 2.0f)
            {
                Frame(ui, ImVec2(from.x + direction.x * t, from.y + direction.y * t), false);
                const bool hovered{ ImGui::IsAnyItemHovered() };
                if (hovered && !wasHovered)
                {
                    found[count++] = ImVec2(from.x + direction.x * (t + 6.0f), from.y + direction.y * (t + 6.0f));
                }
                wasHovered = hovered;
            }
            return count;
        }

        // Moves the mouse down a vertical line: FindHoverablesOnLine
        template <typename Ui>
        int FindHoverables(const Ui& ui, float x, float top, float bottom, ImVec2* found, int maxFound)
        {
            return FindHoverablesOnLine(ui, ImVec2(x, top), ImVec2(x, bottom), found, maxFound);
        }

        // Right-clicks at `at`, then clicks row `row` (0: the first) of the context menu's
        // "Copy as" submenu. The menu opens at the mouse; scanning down from there, "Copy as" is
        // the last hover region (a separator gap sets it apart, or it is the menu's only entry).
        // Its submenu opens beside it on hover, with its first row level with it. False if the
        // menu or the submenu wasn't found; a row past the last clicks outside and copies nothing.
        template <typename Ui>
        bool CopyFromMenu(const Ui& ui, ImVec2 at, int row)
        {
            constexpr int MAX_REGIONS{ 16 };
            constexpr float MENU_SCAN{ 500.0f };
            RightClick(ui, at);

            ImVec2 regions[MAX_REGIONS]{};
            const int regionCount{ FindHoverablesOnLine(ui, ImVec2(at.x + 20.0f, at.y), ImVec2(at.x + 20.0f, at.y + MENU_SCAN),
                                                        regions, MAX_REGIONS) };
            if (regionCount == 0)
            {
                return false;
            }
            const ImVec2 copyAs{ regions[regionCount - 1] };
            Frame(ui, copyAs, false); // Hovered: opens the submenu
            Frame(ui, copyAs, false);

            // "Copy as" itself, then the submenu's first row
            ImVec2 across[2]{};
            if (FindHoverablesOnLine(ui, copyAs, ImVec2(copyAs.x + MENU_SCAN, copyAs.y), across, 2) < 2)
            {
                return false;
            }

            // MIRRORS ImGui's menus: one row per text line plus the item spacing. The hovered item's
            // ID, which would find rows by content, is internal.
            const float pitch{ ImGui::GetTextLineHeightWithSpacing() };
            Click(ui, ImVec2(across[1].x, across[1].y + pitch * static_cast<float>(row)));
            return true;
        }

        // Modifier keys held from the next frame on: ImGui's drag speed tweaks
        void SetModifiers(bool alt, bool shift)
        {
            m_alt = alt;
            m_shift = shift;
        }

        // The color payload seen during the last Drag, if any
        const Payload& LastPayload() const { return m_payload; }

        // The last text ImGui::SetClipboardText gave, through the hooks set in the constructor
        const std::string& Clipboard() const { return m_clipboard; }

        // The topmost open popup's viewport, or nullptr. Internals, as PopupRect.
        ImGuiViewport* PopupViewport() const
        {
            const ImGuiContext& g{ *ImGui::GetCurrentContext() };
            if (g.OpenPopupStack.Size == 0 || g.OpenPopupStack.back().Window == nullptr)
            {
                return nullptr;
            }
            return g.OpenPopupStack.back().Window->Viewport;
        }

        // The topmost open popup, or an empty rect
        ItemRect PopupRect() const
        {
            const ImGuiContext& g{ *ImGui::GetCurrentContext() };
            if (g.OpenPopupStack.Size == 0 || g.OpenPopupStack.back().Window == nullptr)
            {
                return {};
            }
            const ImGuiWindow* popup{ g.OpenPopupStack.back().Window };
            return { popup->Pos, ImVec2(popup->Pos.x + popup->Size.x, popup->Pos.y + popup->Size.y) };
        }

    private:
#ifdef IMGUI_HAS_VIEWPORT
        // Keeps each window's position and size in memory, with one monitor, so ImGui runs its real
        // viewport code. With ConfigViewportsNoAutoMerge every window gets a viewport; popups, menus
        // and tooltips only when they stick out of their parent's.
        struct FakePlatformWindow
        {
            ImVec2 pos{};
            ImVec2 size{};
            bool owned{ true }; // False for the main viewport's, which the harness owns
        };

        static FakePlatformWindow* FakeWindow(ImGuiViewport* viewport)
        {
            return static_cast<FakePlatformWindow*>(viewport->PlatformUserData);
        }

        void InstallFakePlatform()
        {
            ImGuiIO& io{ ImGui::GetIO() };
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
            io.ConfigViewportsNoAutoMerge = true;
            io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_RendererHasViewports;

            ImGuiPlatformIO& platformIo{ ImGui::GetPlatformIO() };
            platformIo.Platform_CreateWindow = [](ImGuiViewport* viewport) { viewport->PlatformUserData = IM_NEW(FakePlatformWindow)(); };
            platformIo.Platform_DestroyWindow = [](ImGuiViewport* viewport)
            {
                FakePlatformWindow* window{ FakeWindow(viewport) };
                if (window != nullptr && window->owned)
                {
                    IM_DELETE(window);
                }
                viewport->PlatformUserData = nullptr;
            };
            platformIo.Platform_ShowWindow = [](ImGuiViewport*) {};
            platformIo.Platform_SetWindowPos = [](ImGuiViewport* viewport, ImVec2 pos) { FakeWindow(viewport)->pos = pos; };
            platformIo.Platform_GetWindowPos = [](ImGuiViewport* viewport) { return FakeWindow(viewport)->pos; };
            platformIo.Platform_SetWindowSize = [](ImGuiViewport* viewport, ImVec2 size) { FakeWindow(viewport)->size = size; };
            platformIo.Platform_GetWindowSize = [](ImGuiViewport* viewport) { return FakeWindow(viewport)->size; };
            platformIo.Platform_SetWindowFocus = [](ImGuiViewport*) {};
            platformIo.Platform_GetWindowFocus = [](ImGuiViewport*) { return true; };
            platformIo.Platform_GetWindowMinimized = [](ImGuiViewport*) { return false; };
            platformIo.Platform_SetWindowTitle = [](ImGuiViewport*, const char*) {};

            ImGuiPlatformMonitor monitor{};
            monitor.MainPos = monitor.WorkPos = ImVec2(0.0f, 0.0f);
            monitor.MainSize = monitor.WorkSize = io.DisplaySize;
            platformIo.Monitors.push_back(monitor);

            m_mainWindow.size = io.DisplaySize;
            m_mainWindow.owned = false;
            ImGui::GetMainViewport()->PlatformUserData = &m_mainWindow;
        }

        FakePlatformWindow m_mainWindow{};
#endif
#ifdef IMGUI_HAS_DOCK
        bool m_docked{ false };
#endif
        Payload m_payload{};
        bool m_alt{ false };
        bool m_shift{ false };
        bool m_ctrl{ false };
        bool m_right{ false };
        std::string m_clipboard{};
    };
}