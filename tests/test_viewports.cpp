// Multi-viewport tests on the harness's fake platform. Empty on master.

#include "imgui.h"
#ifdef IMGUI_HAS_VIEWPORT

#include <doctest/doctest.h>
#include <string>
#include "imok.h"
#include "imok_internal.h"
#include "widget_harness.h"

namespace
{
    // A small window, as one dragged out of the main window. What opens from it sticks out of
    // it, so ImGui gives it a viewport of its own; a popup that fits stays in its parent's.
    template <typename Widget>
    void InSmallWindow(const char* name, ImVec2 pos, const Widget& widget)
    {
        const ImGuiStyle& style{ ImGui::GetStyle() };
        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(ImVec2(ImOkTest::ITEM_WIDTH + style.WindowPadding.x * 2.0f + 40.0f, 70.0f));
        ImGui::Begin(name, nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
        ImGui::PushItemWidth(ImOkTest::ITEM_WIDTH);
        widget();
        ImGui::PopItemWidth();
        ImGui::End();
    }

    const ImVec2 SMALL_POS{ 100.0f, 100.0f };
    const ImVec2 OTHER_POS{ 820.0f, 100.0f }; // Right of the harness's 800 px window
}

TEST_CASE("Viewports: ColorEdit's popup opens under its swatch, in its own viewport, and edits col")
{
    ImOkTest::Harness harness{ ImOkTest::HarnessMode::Viewports };
    float col[3]{ 0.8f, 0.3f, 0.2f };
    ImOkTest::ItemRect edit{};
    ImGuiViewport* windowViewport{ nullptr };
    auto ui = [&]()
    {
        InSmallWindow("Small", SMALL_POS, [&]()
        {
            ImOk::ColorEdit3("##edit", col, ImOkStoredAs_EncodedSrgb);
            edit = ImOkTest::LastItemRect();
            windowViewport = ImGui::GetWindowViewport();
        });
    };
    harness.Settle(ui, 4);
    harness.Click(ui, ImOkTest::SwatchCenter(edit));
    harness.Settle(ui, 4);

    const ImOkTest::ItemRect popup{ harness.PopupRect() };
    REQUIRE(harness.PopupViewport() != nullptr);
    CHECK(harness.PopupViewport() != windowViewport);
    const float frame{ ImGui::GetFrameHeight() };
    CHECK(popup.min.x == doctest::Approx(ImOkTest::SwatchCenter(edit).x - frame * 0.5f));
    CHECK(popup.min.y == doctest::Approx(edit.min.y + frame + ImGui::GetStyle().ItemSpacing.y));

    // A pick in the popup, a viewport of its own, reaches col through state kept in Small's
    // storage. MIRRORS imok.cpp PickerPopupContent: with a hidden label the square starts at
    // the padding; near its top left is a light, unsaturated color.
    const ImGuiStyle& style{ ImGui::GetStyle() };
    harness.Click(ui, ImVec2(popup.min.x + style.WindowPadding.x + 20.0f, popup.min.y + style.WindowPadding.y + 20.0f));
    CHECK((col[0] != 0.8f || col[1] != 0.3f || col[2] != 0.2f));
}

TEST_CASE("Viewports: the copy menu, its submenu in its own viewport, reaches the clipboard")
{
    ImOkTest::Harness harness{ ImOkTest::HarnessMode::Viewports };
    float col[4]{ 0.2f, 0.1f, 0.05f, 0.5f };
    ImOkTest::ItemRect edit{};
    auto ui = [&]()
    {
        InSmallWindow("Small", SMALL_POS, [&]()
        {
            ImOk::ColorEdit4("##edit", col, ImOkStoredAs_LinearSrgb);
            edit = ImOkTest::LastItemRect();
        });
    };
    harness.Settle(ui, 4);

    char expected[ImOk::Internal::COPY_BUFFER_SIZE]{};
    ImOk::Internal::CopyText(ImOk::Internal::CopyFormat::Hex, col, &col[3], ImOkStoredAs_LinearSrgb, expected);
    REQUIRE(harness.CopyFromMenu(ui, edit.Left(), 3)); // Rows: stored, other, bytes, hex
    CHECK(harness.Clipboard() == expected);
}

TEST_CASE("Viewports: the swatch tooltip shows, in a viewport of its own")
{
    ImOkTest::Harness harness{ ImOkTest::HarnessMode::Viewports };
    float col[3]{ 204.0f / 255.0f, 76.0f / 255.0f, 51.0f / 255.0f };
    ImOkTest::ItemRect edit{};
    auto ui = [&]()
    {
        InSmallWindow("Small", SMALL_POS, [&]()
        {
            ImGui::LogToClipboard();
            ImOk::ColorEdit3("##edit", col, ImOkStoredAs_EncodedSrgb);
            edit = ImOkTest::LastItemRect();
            ImGui::LogFinish();
        });
    };
    harness.Settle(ui, 4);
    const int before{ ImGui::GetPlatformIO().Viewports.Size };
    harness.Hover(ui, ImOkTest::SwatchCenter(edit));
    CHECK(ImGui::GetPlatformIO().Viewports.Size > before);
    CHECK(harness.Clipboard().find("Encoded sRGB (stored): 0.8000, 0.2980, 0.2000") != std::string::npos);
}

TEST_CASE("Viewports: a swatch dragged from one viewport drops on a widget in another")
{
    ImOkTest::Harness harness{ ImOkTest::HarnessMode::Viewports };
    float source[3]{ 204.0f / 255.0f, 76.0f / 255.0f, 51.0f / 255.0f };
    float target[3]{ 0.1f, 0.6f, 0.3f };
    ImOkTest::ItemRect sourceEdit{};
    ImOkTest::ItemRect targetEdit{};
    ImGuiViewport* sourceViewport{ nullptr };
    ImGuiViewport* targetViewport{ nullptr };
    auto ui = [&]()
    {
        InSmallWindow("Small", SMALL_POS, [&]()
        {
            ImOk::ColorEdit3("##source", source, ImOkStoredAs_EncodedSrgb);
            sourceEdit = ImOkTest::LastItemRect();
            sourceViewport = ImGui::GetWindowViewport();
        });
        InSmallWindow("Other", OTHER_POS, [&]()
        {
            ImOk::ColorEdit3("##target", target, ImOkStoredAs_EncodedSrgb);
            targetEdit = ImOkTest::LastItemRect();
            targetViewport = ImGui::GetWindowViewport();
        });
    };
    harness.Settle(ui, 4);
    REQUIRE(sourceViewport != targetViewport);

    harness.Drag(ui, ImOkTest::SwatchCenter(sourceEdit), targetEdit.Left());
    CHECK(target[0] == source[0]); // Encoded to encoded, in range: bit-exact
    CHECK(target[1] == source[1]);
    CHECK(target[2] == source[2]);
}

TEST_CASE("Viewports: LightEdit's popup opens under its swatch, in its own viewport")
{
    ImOkTest::Harness harness{ ImOkTest::HarnessMode::Viewports };
    float col[3]{ 1.0f, 0.5f, 0.25f };
    float kelvin{ 2700.0f };
    float intensity{ 800.0f };
    ImOkTest::ItemRect edit{};
    ImGuiViewport* windowViewport{ nullptr };
    auto ui = [&]()
    {
        InSmallWindow("Small", SMALL_POS, [&]()
        {
            ImOk::LightEdit("##light", ImOkLightKind_Point, col, ImOkStoredAs_LinearSrgb, &kelvin, &intensity,
                            ImOkLightUnit_Lumen);
            edit = ImOkTest::LastItemRect();
            windowViewport = ImGui::GetWindowViewport();
        });
    };
    harness.Settle(ui, 4);
    harness.Click(ui, ImOkTest::LightSwatchCenter(edit));
    harness.Settle(ui, 4);

    const ImOkTest::ItemRect popup{ harness.PopupRect() };
    REQUIRE(harness.PopupViewport() != nullptr);
    CHECK(harness.PopupViewport() != windowViewport);
    CHECK(popup.min.x == doctest::Approx(edit.min.x));
    CHECK(popup.min.y == doctest::Approx(edit.min.y + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y));
}

#endif