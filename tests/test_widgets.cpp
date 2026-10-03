#include <doctest/doctest.h>
#include <cmath>
#include <cstring>
#include "imok.h"
#include "imok_internal.h"
#include "widget_harness.h"

// The conversions are pinned in test_dragdrop.cpp; this pins that the widgets use them

using ImOkTest::Harness;
using ImOkTest::ItemRect;
using ImOkTest::LastItemRect;
using ImOkTest::SwatchCenter;

namespace
{
    float LargestLinear(const float col[3], ImOkStoredAs storage)
    {
        const ImOk::LinearSrgb linear{ ImOk::Internal::StoredToLinearSrgb(col, storage) };
        return std::fmax(std::fmax(linear.r, linear.g), linear.b);
    }

    // ImGui names its tooltip windows "##Tooltip_00" and up
    bool TooltipShown()
    {
        const ImGuiWindow* tooltip{ ImGui::FindWindowByName("##Tooltip_00") };
        return tooltip != nullptr && tooltip->Active;
    }

    // MIRRORS imok.cpp SaveSurfaceState: a stored-RGB widget's remembered surface value (hue and
    // the square's two values), read in the widget's ID scope. Call right after the widget.
    struct Remembered
    {
        float h{ 0.0f };
        float x{ 0.0f };
        float y{ 0.0f };
    };

    Remembered RememberedValue(const char* label)
    {
        ImGui::PushID(label);
        ImGuiStorage* stateStorage{ ImGui::GetStateStorage() };
        const Remembered value{ stateStorage->GetFloat(ImGui::GetID("value.h")),
                                stateStorage->GetFloat(ImGui::GetID("value.x")),
                                stateStorage->GetFloat(ImGui::GetID("value.y")) };
        ImGui::PopID();
        return value;
    }

    // A gray level dropped from ImGui's ColorEdit3 (so the payload is exactly the level) on a
    // ColorPicker3 that starts at an orange, stored encoded: the remembered value before and
    // after, and col after
    struct DropOutcome
    {
        Remembered before{};
        Remembered after{};
        float col[3]{};
    };

    DropOutcome DropLevelOnPicker(ImOkColorEditFlags flags, float level)
    {
        Harness harness{};
        float picker[3]{ 0.8f, 0.3f, 0.2f };
        float source[3]{ level, level, level };
        Remembered remembered{};
        ItemRect pickerRect{};
        ItemRect sourceRect{};
        const auto ui{ [&]
        {
            ImOk::ColorPicker3("picker", picker, ImOkStoredAs_EncodedSrgb, flags);
            pickerRect = LastItemRect();
            remembered = RememberedValue("picker");
            ImGui::ColorEdit3("source", source);
            sourceRect = LastItemRect();
        } };
        harness.Settle(ui);

        DropOutcome outcome{};
        outcome.before = remembered;
        harness.Drag(ui, SwatchCenter(sourceRect), pickerRect.Left());
        outcome.after = remembered;
        std::memcpy(outcome.col, picker, sizeof(picker));
        return outcome;
    }
}

TEST_CASE("NoBrightness: picking leaves the largest linear channel exactly 1")
{
    Harness harness{};
    float picked[3]{ 1.0f, 0.5f, 0.2f };
    ItemRect pickerRect{};
    const auto ui{ [&]
    {
        ImOk::ColorPicker3("picker", picked, ImOkStoredAs_LinearSrgb, ImOkColorEditFlags_NoBrightness);
        pickerRect = LastItemRect();
    } };
    harness.Settle(ui);

    // Across the hue x saturation rect, in steps
    for (int i{ 0 }; i < 10; ++i)
    {
        const float t{ (static_cast<float>(i) + 0.5f) / 10.0f };
        harness.Click(ui, ImVec2(pickerRect.min.x + (pickerRect.max.x - pickerRect.min.x) * t * 0.9f,
                                 pickerRect.min.y + (pickerRect.max.y - pickerRect.min.y) * t * 0.9f));
        CAPTURE(i);
        CHECK(LargestLinear(picked, ImOkStoredAs_LinearSrgb) == 1.0f);
    }
}

TEST_CASE("NoBrightness: a darker col from outside is shown raised, not written")
{
    Harness harness{};
    float dark[3]{ 0.2f, 0.1f, 0.05f };
    float shownValue{ 0.0f };
    const auto ui{ [&]
    {
        ImOk::ColorPicker3("picker", dark, ImOkStoredAs_LinearSrgb, ImOkColorEditFlags_NoBrightness);
        // MIRRORS imok.cpp SaveSurfaceState: the Okhsv v the picker shows, in its ID scope
        ImGui::PushID("picker");
        shownValue = ImGui::GetStateStorage()->GetFloat(ImGui::GetID("value.y"));
        ImGui::PopID();
        ImOk::ColorEdit3("edit", dark, ImOkStoredAs_LinearSrgb, ImOkColorEditFlags_NoBrightness);
    } };
    harness.Settle(ui, 5);

    CHECK(shownValue == 1.0f);
    CHECK(dark[0] == 0.2f);
    CHECK(dark[1] == 0.1f);
    CHECK(dark[2] == 0.05f);
}

TEST_CASE("NoBrightness: a darker drop is raised; one already at 1 stays bit-exact")
{
    Harness harness{};
    float darkSource[3]{ 0.4f, 0.2f, 0.1f }; // Encoded, as ImGui's convention
    float brightSource[3]{ 1.0f, 0.123456789f, 0.5f };
    float linearTarget[3]{ 1.0f, 1.0f, 1.0f };
    float encodedTarget[3]{ 1.0f, 1.0f, 1.0f };
    ItemRect darkRect{};
    ItemRect brightRect{};
    ItemRect linearRect{};
    ItemRect encodedRect{};
    const auto ui{ [&]
    {
        ImGui::ColorEdit3("dark", darkSource);
        darkRect = LastItemRect();
        ImGui::ColorEdit3("bright", brightSource);
        brightRect = LastItemRect();
        ImOk::ColorEdit3("linear", linearTarget, ImOkStoredAs_LinearSrgb, ImOkColorEditFlags_NoBrightness);
        linearRect = LastItemRect();
        ImOk::ColorEdit3("encoded", encodedTarget, ImOkStoredAs_EncodedSrgb, ImOkColorEditFlags_NoBrightness);
        encodedRect = LastItemRect();
    } };
    harness.Settle(ui);

    harness.Drag(ui, SwatchCenter(darkRect), linearRect.Left());
    const ImOk::LinearSrgb expected{ ImOk::Internal::ScaleToLargestChannel(ImOk::EncodedSrgbToLinearSrgb({ 0.4f, 0.2f, 0.1f })) };
    CHECK(linearTarget[0] == expected.r);
    CHECK(linearTarget[1] == expected.g);
    CHECK(linearTarget[2] == expected.b);

    harness.Drag(ui, SwatchCenter(brightRect), encodedRect.Left());
    CHECK(encodedTarget[0] == brightSource[0]);
    CHECK(encodedTarget[1] == brightSource[1]);
    CHECK(encodedTarget[2] == brightSource[2]);
}

TEST_CASE("NoBrightness: RGB fields are shown but not editable; alpha still is")
{
    Harness harness{};
    float col[4]{ 1.0f, 0.5f, 0.2f, 0.5f };
    ItemRect editRect{};
    const auto ui{ [&]
    {
        ImOk::ColorEdit4("edit", col, ImOkStoredAs_LinearSrgb,
                         ImOkColorEditFlags_NoBrightness | ImOkColorEditFlags_DisplayRgb);
        editRect = LastItemRect();
    } };
    harness.Settle(ui);

    // MIRRORS imok.cpp ColorEditImpl and FieldWidth: four fields share the width left of the
    // swatch; R is the first quarter, A the last
    const float frame{ ImGui::GetFrameHeight() };
    const float inputs{ ImOkTest::ITEM_WIDTH - frame - ImGui::GetStyle().ItemInnerSpacing.x };
    const float y{ (editRect.min.y + editRect.max.y) * 0.5f };
    const ImVec2 rField{ editRect.min.x + inputs * 0.125f, y };
    const ImVec2 aField{ editRect.min.x + inputs * 0.875f, y };

    harness.Drag(ui, rField, ImVec2(rField.x - 60.0f, y));
    CHECK(col[0] == 1.0f);
    CHECK(col[1] == 0.5f);
    CHECK(col[2] == 0.2f);

    harness.Drag(ui, aField, ImVec2(aField.x + 60.0f, y));
    CHECK(col[3] > 0.5f);
}

TEST_CASE("ImOk sends the stored values exactly, 3 floats from 3-variants")
{
    Harness harness{};
    float source[3]{ 0.2f, 0.4f, 0.123456789f };
    float target[4]{ 0.9f, 0.9f, 0.9f, 0.9f };
    ItemRect sourceRect{};
    ItemRect targetRect{};
    const auto ui{ [&]
    {
        ImOk::ColorEdit3("source", source, ImOkStoredAs_EncodedSrgb);
        sourceRect = LastItemRect();
        ImGui::ColorEdit4("target", target);
        targetRect = LastItemRect();
    } };
    harness.Settle(ui);

    harness.Drag(ui, SwatchCenter(sourceRect), targetRect.Left());

    // Not the 8-bit swatch color (31/255), and 3 floats as ImGui::ColorEdit3 sends, so ImGui's ColorEdit4 keeps its alpha
    const ImOkTest::Payload& payload{ harness.LastPayload() };
    CHECK(std::strcmp(payload.type, IMGUI_PAYLOAD_TYPE_COLOR_3F) == 0);
    CHECK(payload.data[2] == source[2]);
    CHECK(target[0] == source[0]);
    CHECK(target[1] == source[1]);
    CHECK(target[2] == source[2]);
    CHECK(target[3] == 0.9f);
}

TEST_CASE("4-variants send encoded sRGB and exact alpha")
{
    Harness harness{};
    float source[4]{ 0.5f, 0.25f, 0.125f, 0.3f }; // Linear light
    float target[3]{};
    ItemRect sourceRect{};
    ItemRect targetRect{};
    const auto ui{ [&]
    {
        ImOk::ColorEdit4("source", source, ImOkStoredAs_LinearSrgb);
        sourceRect = LastItemRect();
        ImGui::ColorEdit3("target", target);
        targetRect = LastItemRect();
    } };
    harness.Settle(ui);

    harness.Drag(ui, SwatchCenter(sourceRect), targetRect.Left());

    // ImGui's payloads are encoded sRGB by convention: linear storage is encoded on the way out
    const ImOk::EncodedSrgb encoded{ ImOk::LinearSrgbToEncodedSrgb({ source[0], source[1], source[2] }) };
    const ImOkTest::Payload& payload{ harness.LastPayload() };
    CHECK(std::strcmp(payload.type, IMGUI_PAYLOAD_TYPE_COLOR_4F) == 0);
    CHECK(payload.data[0] == encoded.r);
    CHECK(payload.data[1] == encoded.g);
    CHECK(payload.data[2] == encoded.b);
    CHECK(payload.data[3] == 0.3f);
    CHECK(target[0] == encoded.r);
}

TEST_CASE("A drop from ImGui is converted to the widget's storage")
{
    Harness harness{};
    float source[3]{ 0.8f, 0.3f, 0.2f }; // ImGui's convention: encoded sRGB
    float target[3]{};
    bool returnedTrue{ false };
    ItemRect sourceRect{};
    ItemRect targetRect{};
    const auto ui{ [&]
    {
        ImGui::ColorEdit3("source", source);
        sourceRect = LastItemRect();
        returnedTrue |= ImOk::ColorEdit3("target", target, ImOkStoredAs_LinearSrgb);
        targetRect = LastItemRect();
    } };
    harness.Settle(ui);

    harness.Drag(ui, SwatchCenter(sourceRect), targetRect.Left());

    const ImOk::LinearSrgb decoded{ ImOk::EncodedSrgbToLinearSrgb({ 0.8f, 0.3f, 0.2f }) };
    CHECK(target[0] == decoded.r);
    CHECK(target[1] == decoded.g);
    CHECK(target[2] == decoded.b);
    CHECK(returnedTrue);
}

TEST_CASE("EncodedSrgb storage to EncodedSrgb storage is bit-exact")
{
    Harness harness{};
    float source[3]{ 0.3f, 0.123456789f, 0.7f };
    float target[3]{};
    ItemRect sourceRect{};
    ItemRect targetRect{};
    const auto ui{ [&]
    {
        ImOk::ColorEdit3("source", source, ImOkStoredAs_EncodedSrgb);
        sourceRect = LastItemRect();
        ImOk::ColorEdit3("target", target, ImOkStoredAs_EncodedSrgb);
        targetRect = LastItemRect();
    } };
    harness.Settle(ui);

    harness.Drag(ui, SwatchCenter(sourceRect), targetRect.Left());

    CHECK(std::memcmp(source, target, sizeof(source)) == 0);
}

TEST_CASE("The popup's Original swatch drags out, while the popup stays open")
{
    Harness harness{};
    float edited[3]{ 0.2f, 0.4f, 0.6f };
    float target[4]{ 0.9f, 0.9f, 0.9f, 0.9f };
    ItemRect editedRect{};
    ItemRect targetRect{};
    const auto ui{ [&]
    {
        ImOk::ColorEdit3("edited", edited, ImOkStoredAs_EncodedSrgb);
        editedRect = LastItemRect();
        ImGui::ColorEdit4("target", target);
        targetRect = LastItemRect();
    } };
    harness.Settle(ui);

    // Open the popup (the color at this moment becomes "Original"), then change the color, so
    // Original and Current differ
    harness.Click(ui, SwatchCenter(editedRect));
    const float original[3]{ edited[0], edited[1], edited[2] };
    edited[0] = 0.9f;
    harness.Settle(ui);

    const ItemRect popup{ harness.PopupRect() };
    REQUIRE(popup.max.x > popup.min.x);
    ImVec2 swatches[2]{};
    REQUIRE(harness.FindHoverables(ui, ImOkTest::ColorPopupColumnX(popup), popup.min.y, popup.max.y, swatches, 2) == 2);

    harness.Drag(ui, swatches[1], targetRect.Left());

    CHECK(target[0] == original[0]);
    CHECK(target[1] == original[1]);
    CHECK(target[2] == original[2]);
    CHECK(ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId));
}

TEST_CASE("Disabled widgets accept no drops, unlike ImGui's own")
{
    Harness harness{};
    float source[3]{ 0.8f, 0.3f, 0.2f };
    float disabledImOk[3]{};
    float disabledImGui[3]{};
    ItemRect sourceRect{};
    ItemRect imokRect{};
    ItemRect imguiRect{};
    const auto ui{ [&]
    {
        ImGui::ColorEdit3("source", source);
        sourceRect = LastItemRect();
        ImGui::BeginDisabled();
        ImOk::ColorEdit3("imok", disabledImOk, ImOkStoredAs_EncodedSrgb);
        imokRect = LastItemRect();
        ImGui::ColorEdit3("imgui", disabledImGui);
        imguiRect = LastItemRect();
        ImGui::EndDisabled();
    } };
    harness.Settle(ui);

    harness.Drag(ui, SwatchCenter(sourceRect), imokRect.Left());
    CHECK(disabledImOk[0] == 0.0f);

    // Pinned so a change in ImGui is noticed: 1.92.9b's ColorEdit only checks its internal
    // ReadOnly flag, so it takes drops while disabled. If this starts failing, ImGui changed.
    harness.Drag(ui, SwatchCenter(sourceRect), imguiRect.Left());
    CHECK(disabledImGui[0] == 0.8f);
}

TEST_CASE("NoDragDrop: the swatch sends nothing and the widget accepts nothing")
{
    Harness harness{};
    float source[3]{ 0.8f, 0.3f, 0.2f };
    float flagged[3]{ 0.1f, 0.1f, 0.1f };
    ItemRect sourceRect{};
    ItemRect flaggedRect{};
    const auto ui{ [&]
    {
        ImGui::ColorEdit3("source", source);
        sourceRect = LastItemRect();
        ImOk::ColorEdit3("flagged", flagged, ImOkStoredAs_EncodedSrgb, ImOkColorEditFlags_NoDragDrop);
        flaggedRect = LastItemRect();
    } };
    harness.Settle(ui);

    harness.Drag(ui, SwatchCenter(sourceRect), flaggedRect.Left());
    CHECK(flagged[0] == 0.1f);

    harness.Drag(ui, SwatchCenter(flaggedRect), sourceRect.Left());
    CHECK(harness.LastPayload().count == 0);
    CHECK(source[0] == 0.8f);
}

TEST_CASE("NoDragDrop lets a target around the widget get the drop")
{
    // A palette row that inserts dropped colors, with an entry's widget inside it. The smaller
    // target wins in ImGui, so without the flag the entry would take every drop.
    Harness harness{};
    float source[3]{ 0.8f, 0.3f, 0.2f };
    float entries[2][3]{ { 0.1f, 0.1f, 0.1f }, { 0.1f, 0.1f, 0.1f } };
    int rowDrops[2]{};
    ItemRect sourceRect{};
    ItemRect entryRects[2]{};
    const auto ui{ [&]
    {
        ImGui::ColorEdit3("source", source);
        sourceRect = LastItemRect();
        for (int row{ 0 }; row < 2; ++row)
        {
            ImGui::PushID(row);
            ImGui::BeginGroup();
            ImGui::TextUnformatted("Palette row: drop to insert");
            ImOk::ColorEdit3("entry", entries[row], ImOkStoredAs_EncodedSrgb,
                             (row == 1) ? ImOkColorEditFlags_NoDragDrop : ImOkColorEditFlags_None);
            entryRects[row] = LastItemRect();
            ImGui::EndGroup();
            if (ImGui::BeginDragDropTarget())
            {
                if (ImGui::AcceptDragDropPayload(IMGUI_PAYLOAD_TYPE_COLOR_3F))
                {
                    ++rowDrops[row];
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::PopID();
        }
    } };
    harness.Settle(ui);

    harness.Drag(ui, SwatchCenter(sourceRect), entryRects[0].Left());
    CHECK(entries[0][0] == 0.8f); // Without the flag, the entry wins
    CHECK(rowDrops[0] == 0);

    harness.Drag(ui, SwatchCenter(sourceRect), entryRects[1].Left());
    CHECK(entries[1][0] == 0.1f); // With it, the row gets the drop
    CHECK(rowDrops[1] == 1);
}

TEST_CASE("A drop that changes only alpha keeps the remembered hue of a gray")
{
    Harness harness{};
    float picker[4]{ 0.5f, 0.5f, 0.5f, 0.7f };
    float source[4]{};
    float hue{ 0.0f };
    ItemRect pickerRect{};
    ItemRect sourceRect{};
    const auto ui{ [&]
    {
        ImOk::ColorPicker4("picker", picker, ImOkStoredAs_EncodedSrgb);
        pickerRect = LastItemRect();
        ImGui::PushID("picker");
        hue = ImGui::GetStateStorage()->GetFloat(ImGui::GetID("value.h"));
        ImGui::PopID();
        ImGui::ColorEdit4("source", source, ImGuiColorEditFlags_Float);
        sourceRect = LastItemRect();
    } };
    harness.Settle(ui);

    // Pick a hue on the bar: the color stays gray, the picker remembers the hue
    harness.Click(ui, ImOkTest::HueBarPoint(pickerRect, true, 0.4f));
    const float rememberedHue{ hue };
    REQUIRE(rememberedHue > 1.0f);

    // Drop the same RGB, bit for bit, with a new alpha
    source[0] = picker[0];
    source[1] = picker[1];
    source[2] = picker[2];
    source[3] = 0.3f;
    harness.Settle(ui);
    harness.Drag(ui, SwatchCenter(sourceRect), pickerRect.Left());

    CHECK(picker[3] == 0.3f);
    CHECK(hue == rememberedHue);
}

TEST_CASE("Picker drags report IsItemEdited, and IsItemDeactivatedAfterEdit once")
{
    // Engines commit undo on IsItemDeactivatedAfterEdit()
    Harness harness{};
    float stored[3]{ 0.8f, 0.3f, 0.2f };
    ImOk::Okhsv native{ 30.0f, 0.5f, 0.5f };
    float edited[3]{ 0.2f, 0.5f, 0.8f };
    int storedEdited{ 0 };
    int storedCommits{ 0 };
    int nativeEdited{ 0 };
    int nativeCommits{ 0 };
    int editEdited{ 0 };
    int editCommits{ 0 };
    ItemRect storedRect{};
    ItemRect nativeRect{};
    ItemRect editRect{};
    const auto ui{ [&]
    {
        ImOk::ColorPicker3("stored", stored, ImOkStoredAs_EncodedSrgb);
        storedRect = LastItemRect();
        storedEdited += ImGui::IsItemEdited();
        storedCommits += ImGui::IsItemDeactivatedAfterEdit();
        ImOk::ColorPickerOkhsv("native", &native);
        nativeRect = LastItemRect();
        nativeEdited += ImGui::IsItemEdited();
        nativeCommits += ImGui::IsItemDeactivatedAfterEdit();
        ImOk::ColorEdit3("edit", edited, ImOkStoredAs_EncodedSrgb);
        editRect = LastItemRect();
        editEdited += ImGui::IsItemEdited();
        editCommits += ImGui::IsItemDeactivatedAfterEdit();
    } };
    harness.Settle(ui);

    const ImVec2 square{ storedRect.min.x + 40.0f, storedRect.min.y + 40.0f };
    harness.Drag(ui, square, ImVec2(square.x + 60.0f, square.y + 40.0f));
    CHECK(storedEdited > 0);
    CHECK(storedCommits == 1);

    const ImVec2 hueTop{ ImOkTest::HueBarPoint(storedRect, false, 0.1f) };
    harness.Drag(ui, hueTop, ImVec2(hueTop.x, hueTop.y + 80.0f));
    CHECK(storedCommits == 2);

    const ImVec2 nativeSquare{ nativeRect.min.x + 40.0f, nativeRect.min.y + 40.0f };
    harness.Drag(ui, nativeSquare, ImVec2(nativeSquare.x + 60.0f, nativeSquare.y + 20.0f));
    CHECK(nativeEdited > 0);
    CHECK(nativeCommits == 1);

    // The ColorEdit's picker lives in its popup: the widget reports the popup's edits
    harness.Click(ui, SwatchCenter(editRect));
    const ItemRect popup{ harness.PopupRect() };
    REQUIRE(popup.max.x > popup.min.x);
    ImVec2 popupSquare[1]{};
    REQUIRE(harness.FindHoverables(ui, popup.min.x + 40.0f, popup.min.y, popup.max.y, popupSquare, 1) == 1);
    const ImVec2 inSquare{ popupSquare[0].x, popupSquare[0].y + 30.0f };
    harness.Drag(ui, inSquare, ImVec2(inSquare.x + 60.0f, inSquare.y + 40.0f));
    CHECK(editEdited > 0);
    CHECK(editCommits == 1);
}

TEST_CASE("A drop returns true but marks no edit, as ImGui's own drop targets")
{
    // A drop is never an active item, so there is nothing to deactivate. Engines that commit
    // undo on IsItemDeactivatedAfterEdit() miss drops onto ImGui's widgets too: commit when a
    // widget returns true while !IsItemActive().
    Harness harness{};
    float source[3]{ 0.8f, 0.3f, 0.2f };
    float imok[3]{};
    float imgui[3]{};
    int imokReturned{ 0 };
    int imokEdited{ 0 };
    int imguiReturned{ 0 };
    int imguiEdited{ 0 };
    ItemRect sourceRect{};
    ItemRect imokRect{};
    ItemRect imguiRect{};
    const auto ui{ [&]
    {
        ImGui::ColorEdit3("source", source);
        sourceRect = LastItemRect();
        imokReturned += ImOk::ColorEdit3("imok", imok, ImOkStoredAs_EncodedSrgb);
        imokRect = LastItemRect();
        imokEdited += ImGui::IsItemEdited() + ImGui::IsItemDeactivatedAfterEdit();
        imguiReturned += ImGui::ColorEdit3("imgui", imgui);
        imguiRect = LastItemRect();
        imguiEdited += ImGui::IsItemEdited() + ImGui::IsItemDeactivatedAfterEdit();
    } };
    harness.Settle(ui);

    harness.Drag(ui, SwatchCenter(sourceRect), imokRect.Left());
    harness.Drag(ui, SwatchCenter(sourceRect), imguiRect.Left());

    CHECK(imokReturned == 1);
    CHECK(imokEdited == 0);
    CHECK(imguiReturned == 1); // ImGui behaves the same: pinned so a change there is noticed
    CHECK(imguiEdited == 0);
}

TEST_CASE("Native pickers keep their hue when a gray is dropped")
{
    Harness harness{};
    float gray[3]{ 0.5f, 0.5f, 0.5f };
    ImOk::Okhsl native{ 250.0f, 0.8f, 0.6f };
    ItemRect grayRect{};
    ItemRect nativeRect{};
    const auto ui{ [&]
    {
        ImGui::ColorEdit3("gray", gray);
        grayRect = LastItemRect();
        ImOk::ColorPickerOkhsl("native", &native);
        nativeRect = LastItemRect();
    } };
    harness.Settle(ui);

    harness.Drag(ui, SwatchCenter(grayRect), nativeRect.Left());

    CHECK(native.h == 250.0f);
    CHECK(native.s == 0.0f);
    const ImOk::Okhsl expected{ ImOk::OkLabToOkhsl(ImOk::LinearSrgbToOkLab(ImOk::EncodedSrgbToLinearSrgb({ 0.5f, 0.5f, 0.5f }))) };
    CHECK(native.l == expected.l);
}

TEST_CASE("Stored pickers keep what a dropped gray, black or white doesn't define")
{
    // The native pickers' rule (OkLabToOkhsvKeeping). ImGui's ColorEdit resets hue to 0 here.
    // Every subcase starts at an orange, so a reset to 0 would show.
    SUBCASE("Okhsv, gray: hue kept, saturation 0")
    {
        const DropOutcome outcome{ DropLevelOnPicker(ImOkColorEditFlags_PickerOkhsv, 0.5f) };
        REQUIRE(outcome.before.h > 1.0f);
        CHECK(outcome.after.h == outcome.before.h);
        CHECK(outcome.after.x == 0.0f);
        CHECK(outcome.col[0] == 0.5f);
        CHECK(outcome.col[1] == 0.5f);
        CHECK(outcome.col[2] == 0.5f);
    }

    SUBCASE("Okhsv, black: hue and saturation kept")
    {
        const DropOutcome outcome{ DropLevelOnPicker(ImOkColorEditFlags_PickerOkhsv, 0.0f) };
        REQUIRE(outcome.before.h > 1.0f);
        CHECK(outcome.after.h == outcome.before.h);
        CHECK(outcome.after.x == outcome.before.x);
        CHECK(outcome.after.y == 0.0f);
        CHECK(outcome.col[0] == 0.0f);
    }

    SUBCASE("Okhsv, white: hue kept, saturation 0 (a kept s at v = 1 would be vivid)")
    {
        const DropOutcome outcome{ DropLevelOnPicker(ImOkColorEditFlags_PickerOkhsv, 1.0f) };
        REQUIRE(outcome.before.h > 1.0f);
        CHECK(outcome.after.h == outcome.before.h);
        CHECK(outcome.after.x == 0.0f);
        CHECK(outcome.col[0] == 1.0f);
    }

    SUBCASE("Okhsl, gray: hue kept, saturation 0")
    {
        const DropOutcome outcome{ DropLevelOnPicker(ImOkColorEditFlags_PickerOkhsl, 0.5f) };
        REQUIRE(outcome.before.h > 1.0f);
        CHECK(outcome.after.h == outcome.before.h);
        CHECK(outcome.after.x == 0.0f);
    }

    SUBCASE("Okhsl, black and white: hue and saturation kept")
    {
        const DropOutcome onBlack{ DropLevelOnPicker(ImOkColorEditFlags_PickerOkhsl, 0.0f) };
        REQUIRE(onBlack.before.h > 1.0f);
        CHECK(onBlack.after.h == onBlack.before.h);
        CHECK(onBlack.after.x == onBlack.before.x);
        CHECK(onBlack.after.y == 0.0f);

        const DropOutcome onWhite{ DropLevelOnPicker(ImOkColorEditFlags_PickerOkhsl, 1.0f) };
        CHECK(onWhite.after.h == onWhite.before.h);
        CHECK(onWhite.after.x == onWhite.before.x);
        CHECK(onWhite.after.y == 1.0f);
    }

    SUBCASE("NoBrightness, gray: raised to white, hue kept")
    {
        const DropOutcome outcome{ DropLevelOnPicker(ImOkColorEditFlags_NoBrightness, 0.5f) };
        REQUIRE(outcome.before.h > 1.0f);
        CHECK(outcome.after.h == outcome.before.h);
        CHECK(outcome.after.x == 0.0f);
        CHECK(outcome.after.y == 1.0f);
        CHECK(outcome.col[0] == 1.0f);
        CHECK(outcome.col[1] == 1.0f);
        CHECK(outcome.col[2] == 1.0f);
    }
}

TEST_CASE("A gray set from outside resets the remembered hue")
{
    // The other half of the rule: a col from outside may belong to another object (an inspector
    // reusing the ID), so nothing is kept
    Harness harness{};
    float picker[3]{ 0.8f, 0.3f, 0.2f };
    Remembered remembered{};
    const auto ui{ [&]
    {
        ImOk::ColorPicker3("picker", picker, ImOkStoredAs_EncodedSrgb);
        remembered = RememberedValue("picker");
    } };
    harness.Settle(ui);
    REQUIRE(remembered.h > 1.0f);

    picker[0] = 0.5f;
    picker[1] = 0.5f;
    picker[2] = 0.5f;
    harness.Settle(ui);

    CHECK(remembered.h == 0.0f);
}

TEST_CASE("An alpha-only change from outside keeps the remembered hue of a gray")
{
    // Alpha derives nothing, so it is outside the "changed from outside" rule: code that fades
    // alpha (or an undo of an alpha edit) must not reset the hue kept at a gray
    Harness harness{};
    float picker[4]{ 0.5f, 0.5f, 0.5f, 0.7f };
    Remembered remembered{};
    ItemRect pickerRect{};
    const auto ui{ [&]
    {
        ImOk::ColorPicker4("picker", picker, ImOkStoredAs_EncodedSrgb);
        pickerRect = LastItemRect();
        remembered = RememberedValue("picker");
    } };
    harness.Settle(ui);

    // Pick a hue on the bar: the color stays gray, the picker remembers the hue
    harness.Click(ui, ImOkTest::HueBarPoint(pickerRect, true, 0.4f));
    const Remembered before{ remembered };
    REQUIRE(before.h > 1.0f);
    const float gray[3]{ picker[0], picker[1], picker[2] };

    picker[3] = 0.3f;
    harness.Settle(ui);

    CHECK(remembered.h == before.h);
    CHECK(remembered.x == before.x);
    CHECK(remembered.y == before.y);
    CHECK(picker[0] == gray[0]);
    CHECK(picker[1] == gray[1]);
    CHECK(picker[2] == gray[2]);
    CHECK(picker[3] == 0.3f);
}

TEST_CASE("A hex edit of only AA leaves col[0..2] bit-identical")
{
    // Linear storage and a value between bytes: re-deriving RGB from the typed hex would change
    // it. Only the parts whose bytes changed are written (HexField).
    Harness harness{};
    float col[4]{ 0.2f, 0.5f, 0.123456789f, 1.0f };
    ItemRect editRect{};
    const auto ui{ [&]
    {
        ImOk::ColorEdit4("edit", col, ImOkStoredAs_LinearSrgb, ImOkColorEditFlags_DisplayHex);
        editRect = LastItemRect();
    } };
    harness.Settle(ui);

    // The hex shown, with a new AA
    const float rgb[3]{ col[0], col[1], col[2] };
    const float newAlpha{ 0.3f };
    char typed[ImOk::HEX_BUFFER_SIZE]{};
    ImOk::EncodedSrgbToHex(ImOk::LinearSrgbToEncodedSrgb({ col[0], col[1], col[2] }), typed, &newAlpha);
    ImOk::EncodedSrgb parsed{};
    float expectedAlpha{ 0.0f };
    REQUIRE(ImOk::HexToEncodedSrgb(typed, &parsed, &expectedAlpha));

    harness.Type(ui, editRect.Left(), typed);

    CHECK(col[0] == rgb[0]);
    CHECK(col[1] == rgb[1]);
    CHECK(col[2] == rgb[2]);
    CHECK(col[3] == expectedAlpha);
}

TEST_CASE("Typed hex of a gray keeps the remembered hue, as a dropped gray does")
{
    Harness harness{};
    float col[3]{ 0.8f, 0.3f, 0.2f };
    Remembered remembered{};
    ItemRect editRect{};
    const auto ui{ [&]
    {
        ImOk::ColorEdit3("edit", col, ImOkStoredAs_EncodedSrgb, ImOkColorEditFlags_DisplayHex);
        editRect = LastItemRect();
        remembered = RememberedValue("edit");
    } };
    harness.Settle(ui);
    const Remembered before{ remembered };
    REQUIRE(before.h > 1.0f);

    harness.Type(ui, editRect.Left(), "#808080");

    CHECK(col[0] == 128.0f / 255.0f);
    CHECK(col[1] == 128.0f / 255.0f);
    CHECK(col[2] == 128.0f / 255.0f);
    CHECK(remembered.h == before.h);
    CHECK(remembered.x == 0.0f);
}

TEST_CASE("Original on a gray keeps the current hue")
{
    Harness harness{};
    float edited[3]{ 0.5f, 0.5f, 0.5f };
    Remembered remembered{};
    ItemRect editedRect{};
    const auto ui{ [&]
    {
        ImOk::ColorEdit3("edited", edited, ImOkStoredAs_EncodedSrgb);
        editedRect = LastItemRect();
        remembered = RememberedValue("edited");
    } };
    harness.Settle(ui);

    // Open the popup at the gray (it becomes Original), then change the color, as another
    // widget would: the hue is now the orange's
    harness.Click(ui, SwatchCenter(editedRect));
    edited[0] = 0.8f;
    edited[1] = 0.3f;
    edited[2] = 0.2f;
    harness.Settle(ui);
    const float orangeHue{ remembered.h };
    REQUIRE(orangeHue > 1.0f);

    const ItemRect popup{ harness.PopupRect() };
    REQUIRE(popup.max.x > popup.min.x);
    ImVec2 swatches[2]{};
    REQUIRE(harness.FindHoverables(ui, ImOkTest::ColorPopupColumnX(popup), popup.min.y, popup.max.y, swatches, 2) == 2);
    harness.Click(ui, swatches[1]);

    CHECK(edited[0] == 0.5f);
    CHECK(edited[1] == 0.5f);
    CHECK(edited[2] == 0.5f);
    CHECK(remembered.h == orangeHue);
    CHECK(remembered.x == 0.0f);
}

TEST_CASE("LightEdit: a gray dropped on the swatch becomes white, keeping the hue")
{
    Harness harness{};
    float source[3]{ 0.5f, 0.5f, 0.5f };
    float col[3]{ 1.0f, 0.4f, 0.1f }; // Linear, largest channel 1
    float lumens{ 800.0f };
    Remembered remembered{};
    ItemRect sourceRect{};
    ItemRect lightRect{};
    const auto ui{ [&]
    {
        ImGui::ColorEdit3("source", source);
        sourceRect = LastItemRect();
        ImOk::LightEdit("lamp", ImOkLightKind_Point, col, ImOkStoredAs_LinearSrgb, nullptr, &lumens, ImOkLightUnit_Lumen);
        lightRect = LastItemRect();
        remembered = RememberedValue("lamp");
    } };
    harness.Settle(ui);
    const Remembered before{ remembered };
    REQUIRE(before.h > 1.0f);

    harness.Drag(ui, SwatchCenter(sourceRect), ImOkTest::LightSwatchCenter(lightRect));

    CHECK(col[0] == 1.0f);
    CHECK(col[1] == 1.0f);
    CHECK(col[2] == 1.0f);
    CHECK(remembered.h == before.h);
    CHECK(remembered.x == 0.0f);
}

TEST_CASE("TemperatureEdit: the bar reaches both ends and commits once; outside values stay")
{
    Harness harness{};
    float kelvin{ 2700.0f };
    int commits{ 0 };
    ItemRect editRect{};
    const auto ui{ [&]
    {
        ImOk::TemperatureEdit("Temperature", &kelvin);
        editRect = LastItemRect();
        commits += ImGui::IsItemDeactivatedAfterEdit();
    } };
    harness.Settle(ui);

    // The bar starts at the item's left edge
    const float y{ (editRect.min.y + editRect.max.y) * 0.5f };
    harness.Drag(ui, ImVec2(editRect.min.x + 20.0f, y), ImVec2(editRect.min.x - 50.0f, y));
    CHECK(kelvin == ImOk::MIN_KELVIN);
    CHECK(commits == 1);

    harness.Drag(ui, ImVec2(editRect.min.x + 20.0f, y), ImVec2(editRect.max.x + 500.0f, y));
    CHECK(kelvin == doctest::Approx(ImOk::MAX_KELVIN));
    CHECK(commits == 2);

    // Shown clamped, never written back
    kelvin = 40000.0f;
    harness.Settle(ui, 3);
    CHECK(kelvin == 40000.0f);
}

TEST_CASE("IntensityEdit: the bar spans the unit's range; the field drags; outside values stay")
{
    Harness harness{};
    float lux{ 1000.0f };
    float ev{ 12.0f };
    int commits{ 0 };
    ItemRect luxRect{};
    ItemRect evRect{};
    const auto ui{ [&]
    {
        ImOk::IntensityEdit("Sun", &lux, ImOkLightUnit_Lux);
        luxRect = LastItemRect();
        commits += ImGui::IsItemDeactivatedAfterEdit();
        ImOk::IntensityEdit("Exposure", &ev, ImOkLightUnit_EV100);
        evRect = LastItemRect();
    } };
    harness.Settle(ui);

    // The bar starts at the item's left edge
    const float luxY{ (luxRect.min.y + luxRect.max.y) * 0.5f };
    harness.Drag(ui, ImVec2(luxRect.min.x + 60.0f, luxY), ImVec2(luxRect.min.x - 50.0f, luxY));
    CHECK(lux == 0.0f); // Off
    CHECK(commits == 1);
    harness.Drag(ui, ImVec2(luxRect.min.x + 60.0f, luxY), ImVec2(luxRect.max.x + 500.0f, luxY));
    CHECK(lux == doctest::Approx(150000.0f));

    const float evY{ (evRect.min.y + evRect.max.y) * 0.5f };
    harness.Drag(ui, ImVec2(evRect.min.x + 60.0f, evY), ImVec2(evRect.min.x - 50.0f, evY));
    CHECK(ev == -12.0f); // EV can go below 0: dim, not negative light

    // MIRRORS imok.cpp IntensityEdit: the field ends the item width
    lux = 1000.0f;
    harness.Settle(ui);
    const float fieldX{ luxRect.min.x + ImOkTest::ITEM_WIDTH - 20.0f };
    harness.Drag(ui, ImVec2(fieldX, luxY), ImVec2(fieldX + 40.0f, luxY));
    CHECK(lux > 1000.0f);

    // Shown clamped, never written back
    lux = 500000.0f;
    harness.Settle(ui, 3);
    CHECK(lux == 500000.0f);
}

TEST_CASE("Speed tweaks: Alt moves a bar 100x slower, from its value, without jumping")
{
    Harness harness{};
    float kelvin{ 2700.0f };
    ItemRect editRect{};
    const auto ui{ [&]
    {
        ImOk::TemperatureEdit("Temperature", &kelvin);
        editRect = LastItemRect();
    } };
    harness.Settle(ui);

    // MIRRORS imok.cpp TemperatureEdit: the bar is the item width minus the field and spacing
    const ImGuiStyle& style{ ImGui::GetStyle() };
    const float fieldWidth{ ImGui::CalcTextSize("00000 K").x + style.FramePadding.x * 2.0f };
    const float barWidth{ ImOkTest::ITEM_WIDTH - fieldWidth - style.ItemInnerSpacing.x };
    const float y{ (editRect.min.y + editRect.max.y) * 0.5f };
    const float startPosition{ ImOk::Internal::KelvinToBarPosition(kelvin) };

    // Pressed far from the marker: no jump. Moved 100 px: the bar moves 1 px.
    harness.SetModifiers(true, false);
    harness.Drag(ui, ImVec2(editRect.min.x + barWidth * 0.9f, y), ImVec2(editRect.min.x + barWidth * 0.9f - 100.0f, y));
    harness.SetModifiers(false, false);
    CHECK(ImOk::Internal::KelvinToBarPosition(kelvin) == doctest::Approx(startPosition - 1.0f / barWidth).epsilon(1e-3));
}

TEST_CASE("Speed tweaks: Shift moves 10x faster; without keys a press still jumps")
{
    Harness harness{};
    float col[3]{ 0.5f, 0.5f, 0.5f };
    float hue{ 0.0f };
    ItemRect pickerRect{};
    const auto ui{ [&]
    {
        ImOk::ColorPicker3("picker", col, ImOkStoredAs_LinearSrgb);
        pickerRect = LastItemRect();
        ImGui::PushID("picker");
        hue = ImGui::GetStateStorage()->GetFloat(ImGui::GetID("value.h"));
        ImGui::PopID();
    } };
    harness.Settle(ui);

    // No keys: a click jumps the hue to the point, as before (within a pixel: 1.3 degrees)
    const float frame{ ImGui::GetFrameHeight() };
    const float square{ ImOkTest::ITEM_WIDTH - frame - ImGui::GetStyle().ItemInnerSpacing.x };
    harness.Click(ui, ImOkTest::HueBarPoint(pickerRect, false, 0.1f));
    const float clicked{ hue };
    CHECK(std::fabs(clicked - 36.0f) < 360.0f / square);

    // Shift, pressed elsewhere on the bar: no jump, and 5 px moves as far as 50 px would
    const ImVec2 from{ ImOkTest::HueBarPoint(pickerRect, false, 0.8f) };
    harness.SetModifiers(false, true);
    harness.Drag(ui, from, ImVec2(from.x, from.y + 5.0f));
    harness.SetModifiers(false, false);
    CHECK(hue == doctest::Approx(clicked + 50.0f / square * 360.0f).epsilon(1e-3));
}

TEST_CASE("NoReferences: no tooltip over the bar; without it, one")
{
    Harness harness{};
    float plain{ 800.0f };
    float bare{ 800.0f };
    ItemRect plainRect{};
    ItemRect bareRect{};
    const auto ui{ [&]
    {
        ImOk::IntensityEdit("plain", &plain, ImOkLightUnit_Lumen);
        plainRect = LastItemRect();
        ImOk::IntensityEdit("bare", &bare, ImOkLightUnit_Lumen, ImOkLightEditFlags_NoReferences);
        bareRect = LastItemRect();
    } };
    harness.Settle(ui);

    harness.Frame(ui, plainRect.Left(), false);
    harness.Frame(ui, plainRect.Left(), false);
    CHECK(TooltipShown());

    harness.Frame(ui, bareRect.Left(), false);
    harness.Frame(ui, bareRect.Left(), false);
    CHECK_FALSE(TooltipShown());
}

TEST_CASE("NoSpeedTweaks: with Alt held, a press still jumps to the point")
{
    Harness harness{};
    float kelvin{ 2700.0f };
    ItemRect editRect{};
    const auto ui{ [&]
    {
        ImOk::TemperatureEdit("Temperature", &kelvin, ImOkLightEditFlags_NoSpeedTweaks);
        editRect = LastItemRect();
    } };
    harness.Settle(ui);

    // Dragged past the left end with Alt: the plain slider behavior, so the minimum
    const float y{ (editRect.min.y + editRect.max.y) * 0.5f };
    harness.SetModifiers(true, false);
    harness.Drag(ui, ImVec2(editRect.min.x + 60.0f, y), ImVec2(editRect.min.x - 50.0f, y));
    harness.SetModifiers(false, false);
    CHECK(kelvin == ImOk::MIN_KELVIN);
}

TEST_CASE("ImOkColorEditFlags_NoSpeedTweaks: the pickers ignore Alt")
{
    Harness harness{};
    float col[3]{ 0.5f, 0.5f, 0.5f };
    float hue{ 0.0f };
    ItemRect pickerRect{};
    const auto ui{ [&]
    {
        ImOk::ColorPicker3("picker", col, ImOkStoredAs_LinearSrgb, ImOkColorEditFlags_NoSpeedTweaks);
        pickerRect = LastItemRect();
        ImGui::PushID("picker");
        hue = ImGui::GetStateStorage()->GetFloat(ImGui::GetID("value.h"));
        ImGui::PopID();
    } };
    harness.Settle(ui);

    // With Alt held, the click still jumps the hue to the point (within a pixel)
    const float frame{ ImGui::GetFrameHeight() };
    const float square{ ImOkTest::ITEM_WIDTH - frame - ImGui::GetStyle().ItemInnerSpacing.x };
    harness.SetModifiers(true, false);
    harness.Click(ui, ImOkTest::HueBarPoint(pickerRect, false, 0.5f));
    harness.SetModifiers(false, false);
    CHECK(std::fabs(hue - 180.0f) < 360.0f / square);
}

TEST_CASE("Vertical light widgets: the default size, and a fader from bottom (min) to top (max)")
{
    Harness harness{};
    float kelvin{ 2700.0f };
    float lumens{ 800.0f };
    ItemRect temperatureRect{};
    ItemRect intensityRect{};
    const auto ui{ [&]
    {
        ImOk::VTemperatureEdit("##temperature", ImVec2(0.0f, 0.0f), &kelvin);
        temperatureRect = LastItemRect();
        ImGui::SameLine();
        ImOk::VIntensityEdit("##intensity", ImVec2(0.0f, 300.0f), &lumens, ImOkLightUnit_Lumen);
        intensityRect = LastItemRect();
    } };
    harness.Settle(ui);

    // MIRRORS imok.cpp LightBarButton: 8 frame heights by default, field included, and as wide as
    // the field
    const float frame{ ImGui::GetFrameHeight() };
    const ImGuiStyle& style{ ImGui::GetStyle() };
    CHECK(temperatureRect.max.y - temperatureRect.min.y == doctest::Approx(frame * 8.0f));
    CHECK(temperatureRect.max.x - temperatureRect.min.x
          == doctest::Approx(ImGui::CalcTextSize("00000 K").x + style.FramePadding.x * 2.0f));
    CHECK(intensityRect.max.y - intensityRect.min.y == doctest::Approx(300.0f));

    // Dragged below the bar: the minimum; above it: the maximum
    const float x{ (temperatureRect.min.x + temperatureRect.max.x) * 0.5f };
    const float barMiddle{ temperatureRect.min.y + frame * 3.0f };
    harness.Drag(ui, ImVec2(x, barMiddle), ImVec2(x, temperatureRect.max.y + 200.0f));
    CHECK(kelvin == doctest::Approx(ImOk::MIN_KELVIN));
    harness.Drag(ui, ImVec2(x, barMiddle), ImVec2(x, temperatureRect.min.y - 200.0f));
    CHECK(kelvin == doctest::Approx(ImOk::MAX_KELVIN));

    // Alt, upward by 100 px: 1 px of bar, and up is more
    const float lumensX{ (intensityRect.min.x + intensityRect.max.x) * 0.5f };
    const float lumensMiddle{ intensityRect.min.y + 100.0f };
    const ImOk::Internal::LightUnitInfo& info{ ImOk::Internal::GetLightUnitInfo(ImOkLightUnit_Lumen) };
    const float barHeight{ 300.0f - frame - style.ItemSpacing.y };
    const float startPosition{ ImOk::Internal::IntensityToBarPosition(lumens, info) };
    harness.SetModifiers(true, false);
    harness.Drag(ui, ImVec2(lumensX, lumensMiddle), ImVec2(lumensX, lumensMiddle - 100.0f));
    harness.SetModifiers(false, false);
    CHECK(ImOk::Internal::IntensityToBarPosition(lumens, info)
          == doctest::Approx(startPosition + 1.0f / barHeight).epsilon(1e-3));
}

TEST_CASE("LightEdit: its bar and its popup each commit once, as one item")
{
    Harness harness{};
    float col[3]{ 1.0f, 1.0f, 1.0f };
    float kelvin{ 2700.0f };
    float lumens{ 800.0f };
    int edited{ 0 };
    int commits{ 0 };
    ItemRect editRect{};
    const auto ui{ [&]
    {
        ImOk::LightEdit("lamp", ImOkLightKind_Point, col, ImOkStoredAs_LinearSrgb, &kelvin, &lumens, ImOkLightUnit_Lumen);
        editRect = LastItemRect();
        edited += ImGui::IsItemEdited();
        commits += ImGui::IsItemDeactivatedAfterEdit();
    } };
    harness.Settle(ui);

    // The row's intensity bar, dragged past its left end: off
    const ImVec2 bar{ ImOkTest::LightBarPoint(editRect, 20.0f) };
    harness.Drag(ui, bar, ImVec2(editRect.min.x - 50.0f, bar.y));
    CHECK(lumens == 0.0f);
    CHECK(edited > 0);
    CHECK(commits == 1);

    // The popup's square: forwarded through the composite's group, as ColorEdit3's popup
    harness.Click(ui, ImOkTest::LightSwatchCenter(editRect));
    const ItemRect popup{ harness.PopupRect() };
    REQUIRE(popup.max.x > popup.min.x);
    ImVec2 square[1]{};
    REQUIRE(harness.FindHoverables(ui, popup.min.x + 40.0f, popup.min.y, popup.max.y, square, 1) == 1);
    const ImVec2 inSquare{ square[0].x, square[0].y + 30.0f };
    harness.Drag(ui, inSquare, ImVec2(inSquare.x + 60.0f, inSquare.y + 20.0f));
    CHECK(col[0] + col[1] + col[2] < 3.0f); // No longer white
    CHECK(LargestLinear(col, ImOkStoredAs_LinearSrgb) == 1.0f);
    CHECK(kelvin == 2700.0f); // The color is never written into the temperature
    CHECK(commits == 2);
}

TEST_CASE("LightEdit: the swatch takes a drop as the color, raised, but sends none; the bar takes none")
{
    Harness harness{};
    float source[3]{ 0.4f, 0.2f, 0.1f }; // Encoded, as ImGui's convention
    float target[3]{ 0.9f, 0.9f, 0.9f };
    float col[3]{ 1.0f, 1.0f, 1.0f };
    float kelvin{ 2700.0f };
    float lumens{ 800.0f };
    ItemRect sourceRect{};
    ItemRect lightRect{};
    ItemRect targetRect{};
    const auto ui{ [&]
    {
        ImGui::ColorEdit3("source", source);
        sourceRect = LastItemRect();
        ImOk::LightEdit("lamp", ImOkLightKind_Point, col, ImOkStoredAs_LinearSrgb, &kelvin, &lumens, ImOkLightUnit_Lumen);
        lightRect = LastItemRect();
        ImGui::ColorEdit3("target", target);
        targetRect = LastItemRect();
    } };
    harness.Settle(ui);

    // Onto the bar: not a color target
    harness.Drag(ui, SwatchCenter(sourceRect), ImOkTest::LightBarPoint(lightRect, 40.0f));
    CHECK(col[0] == 1.0f);
    CHECK(col[1] == 1.0f);
    CHECK(col[2] == 1.0f);
    CHECK(lumens == 800.0f);

    // Onto the swatch: the color, raised to the largest channel 1; the temperature stays apart
    harness.Drag(ui, SwatchCenter(sourceRect), ImOkTest::LightSwatchCenter(lightRect));
    const ImOk::LinearSrgb expected{ ImOk::Internal::ScaleToLargestChannel(ImOk::EncodedSrgbToLinearSrgb({ 0.4f, 0.2f, 0.1f })) };
    CHECK(col[0] == expected.r);
    CHECK(col[1] == expected.g);
    CHECK(col[2] == expected.b);
    CHECK(kelvin == 2700.0f);

    // Out of the swatch: nothing, since it shows the temperature too
    harness.Drag(ui, ImOkTest::LightSwatchCenter(lightRect), targetRect.Left());
    CHECK(harness.LastPayload().count == 0);
    CHECK(target[0] == 0.9f);
}

TEST_CASE("LightEdit NoDragDrop: the swatch accepts nothing")
{
    Harness harness{};
    float source[3]{ 0.8f, 0.3f, 0.2f };
    float col[3]{ 1.0f, 1.0f, 1.0f };
    float lumens{ 800.0f };
    ItemRect sourceRect{};
    ItemRect lightRect{};
    const auto ui{ [&]
    {
        ImGui::ColorEdit3("source", source);
        sourceRect = LastItemRect();
        ImOk::LightEdit("lamp", ImOkLightKind_Point, col, ImOkStoredAs_LinearSrgb, nullptr, &lumens, ImOkLightUnit_Lumen,
                        ImOkLightEditFlags_NoDragDrop);
        lightRect = LastItemRect();
    } };
    harness.Settle(ui);

    harness.Drag(ui, SwatchCenter(sourceRect), ImOkTest::LightSwatchCenter(lightRect));
    CHECK(col[0] == 1.0f);
    CHECK(col[1] == 1.0f);
    CHECK(col[2] == 1.0f);
}

TEST_CASE("LightEdit's popup: Color drags out the stored color; Original restores all three")
{
    Harness harness{};
    float col[3]{ 1.0f, 1.0f, 1.0f };
    float kelvin{ 2700.0f };
    float lumens{ 800.0f };
    float target[3]{ 0.9f, 0.9f, 0.9f };
    ItemRect lightRect{};
    ItemRect targetRect{};
    const auto ui{ [&]
    {
        ImOk::LightEdit("lamp", ImOkLightKind_Point, col, ImOkStoredAs_LinearSrgb, &kelvin, &lumens, ImOkLightUnit_Lumen);
        lightRect = LastItemRect();
        // Well right of the popup, which opens at the swatch on the left
        ImGui::SameLine(500.0f);
        ImGui::ColorEdit3("target", target);
        targetRect = LastItemRect();
    } };
    harness.Settle(ui);

    // Open the popup (these values become Original), then change all three, as edits would
    harness.Click(ui, ImOkTest::LightSwatchCenter(lightRect));
    col[1] = 0.5f;
    col[2] = 0.2f;
    kelvin = 5000.0f;
    lumens = 100.0f;
    harness.Settle(ui);

    const ItemRect popup{ harness.PopupRect() };
    REQUIRE(popup.max.x > popup.min.x);
    ImVec2 swatches[3]{};
    REQUIRE(harness.FindHoverables(ui, ImOkTest::LightPopupColumnX(popup), popup.min.y, popup.max.y, swatches, 3) == 3);

    // Color sends col exactly, encoded as ImGui's payloads are, without the temperature
    harness.Drag(ui, swatches[0], targetRect.Left());
    const ImOk::EncodedSrgb encoded{ ImOk::LinearSrgbToEncodedSrgb({ col[0], col[1], col[2] }) };
    CHECK(target[0] == encoded.r);
    CHECK(target[1] == encoded.g);
    CHECK(target[2] == encoded.b);

    // Original: one light's edit undone, not one of its parts
    harness.Click(ui, swatches[2]);
    CHECK(col[0] == 1.0f);
    CHECK(col[1] == 1.0f);
    CHECK(col[2] == 1.0f);
    CHECK(kelvin == 2700.0f);
    CHECK(lumens == 800.0f);
    CHECK(ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId));
}

TEST_CASE("LightPicker: without a temperature it has no temperature row, and still edits the color")
{
    Harness harness{};
    float withColor[3]{ 1.0f, 1.0f, 1.0f };
    float withoutColor[3]{ 1.0f, 1.0f, 1.0f };
    float kelvin{ 2700.0f };
    float withNits{ 200.0f };
    float withoutNits{ 200.0f };
    ItemRect withRect{};
    ItemRect withoutRect{};
    const auto ui{ [&]
    {
        ImOk::LightPicker("with", ImOkLightKind_Emissive, withColor, ImOkStoredAs_LinearSrgb, &kelvin, &withNits, ImOkLightUnit_Nits);
        withRect = LastItemRect();
        ImOk::LightPicker("without", ImOkLightKind_Emissive, withoutColor, ImOkStoredAs_LinearSrgb, nullptr, &withoutNits, ImOkLightUnit_Nits);
        withoutRect = LastItemRect();
    } };
    harness.Settle(ui);

    // MIRRORS imok.cpp LightPickerBody: the temperature is one frame-height row
    const float rowHeight{ ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y };
    CHECK((withRect.max.y - withRect.min.y) - (withoutRect.max.y - withoutRect.min.y) == doctest::Approx(rowHeight));

    // The square starts at the top left
    const ImVec2 inSquare{ withoutRect.min.x + 40.0f, withoutRect.min.y + 30.0f };
    harness.Drag(ui, inSquare, ImVec2(inSquare.x + 60.0f, inSquare.y + 20.0f));
    CHECK(withoutColor[0] + withoutColor[1] + withoutColor[2] < 3.0f);
    CHECK(LargestLinear(withoutColor, ImOkStoredAs_LinearSrgb) == 1.0f);
}

// --- Copy menu -----------------------------------------------------------------

namespace
{
    // The rows a "Copy as" submenu shows for this col, in order: formats that don't apply
    // (HexAlpha without alpha) have no row
    int CopyRows(const float col[3], const float* alpha, ImOkStoredAs storage, std::string rows[])
    {
        int count{ 0 };
        for (int i{ 0 }; i < static_cast<int>(ImOk::Internal::CopyFormat::Count); ++i)
        {
            char text[ImOk::Internal::COPY_BUFFER_SIZE]{};
            if (ImOk::Internal::CopyText(static_cast<ImOk::Internal::CopyFormat>(i), col, alpha, storage, text))
            {
                rows[count++] = text;
            }
        }
        return count;
    }
}

TEST_CASE("Copy menu: ColorEdit4 copies col, every row")
{
    ImOkTest::Harness harness{};
    float col[4]{ 0.2f, 0.1f, 0.05f, 0.5f };
    ImOkTest::ItemRect edit{};
    auto ui = [&]()
    {
        ImOk::ColorEdit4("##edit", col, ImOkStoredAs_LinearSrgb);
        edit = ImOkTest::LastItemRect();
    };
    harness.Settle(ui);

    std::string rows[static_cast<int>(ImOk::Internal::CopyFormat::Count)]{};
    const int rowCount{ CopyRows(col, &col[3], ImOkStoredAs_LinearSrgb, rows) };
    CHECK(rowCount == 6);
    for (int row{ 0 }; row < rowCount; ++row)
    {
        CAPTURE(row);
        REQUIRE(harness.CopyFromMenu(ui, edit.Left(), row)); // Left(): on the first field
        CHECK(harness.Clipboard() == rows[row]);
        harness.Settle(ui);
    }

    // Copying reads col, never writes it
    CHECK(col[0] == 0.2f);
    CHECK(col[1] == 0.1f);
    CHECK(col[2] == 0.05f);
    CHECK(col[3] == 0.5f);
}

TEST_CASE("Copy menu: NoBrightness ColorPicker3 has Copy only, and copies col as stored, not raised")
{
    ImOkTest::Harness harness{};
    float col[3]{ 0.2f, 0.1f, 0.05f }; // Darker than largest channel 1: shown raised, not written
    ImOkTest::ItemRect picker{};
    auto ui = [&]()
    {
        ImOk::ColorPicker3("##picker", col, ImOkStoredAs_LinearSrgb, ImOkColorEditFlags_NoBrightness);
        picker = ImOkTest::LastItemRect();
    };
    harness.Settle(ui);

    REQUIRE(harness.CopyFromMenu(ui, picker.Center(), 0));
    CHECK(harness.Clipboard() == "(0.2f, 0.1f, 0.05f)");
    CHECK(col[0] == 0.2f);
}

TEST_CASE("Copy menu: ColorEdit's popup picker copies col")
{
    ImOkTest::Harness harness{};
    float col[3]{ 204.0f / 255.0f, 76.0f / 255.0f, 51.0f / 255.0f };
    ImOkTest::ItemRect edit{};
    auto ui = [&]()
    {
        ImOk::ColorEdit3("##edit", col, ImOkStoredAs_EncodedSrgb);
        edit = ImOkTest::LastItemRect();
    };
    harness.Settle(ui);
    harness.Click(ui, ImOkTest::SwatchCenter(edit));
    harness.Settle(ui);

    const ImOkTest::ItemRect popup{ harness.PopupRect() };
    REQUIRE(popup.max.x > popup.min.x);
    // MIRRORS imok.cpp PickerPopupContent: with a hidden label, the picker square starts at the
    // popup's padding
    const ImGuiStyle& style{ ImGui::GetStyle() };
    const ImVec2 square{ popup.min.x + style.WindowPadding.x + 20.0f, popup.min.y + style.WindowPadding.y + 20.0f };
    REQUIRE(harness.CopyFromMenu(ui, square, 0));
    CHECK(harness.Clipboard() == "(0.8f, 0.29803923f, 0.2f)");
}

TEST_CASE("Copy menu: LightPicker copies the color alone, not color x temperature")
{
    ImOkTest::Harness harness{};
    float col[3]{ 1.0f, 0.5f, 0.25f }; // Largest channel 1: nothing to raise
    float kelvin{ 2700.0f };
    float intensity{ 800.0f };
    ImOkTest::ItemRect picker{};
    auto ui = [&]()
    {
        ImOk::LightPicker("##light", ImOkLightKind_Point, col, ImOkStoredAs_LinearSrgb, &kelvin, &intensity,
                          ImOkLightUnit_Lumen);
        picker = ImOkTest::LastItemRect();
    };
    harness.Settle(ui);

    // MIRRORS imok.cpp LightPickerBody: the color's hue x saturation rect starts at the top left
    REQUIRE(harness.CopyFromMenu(ui, ImVec2(picker.min.x + 20.0f, picker.min.y + 20.0f), 0));
    CHECK(harness.Clipboard() == "(1.0f, 0.5f, 0.25f)");
}

TEST_CASE("Copy menu: LightEdit's row has none (its swatch shows color x temperature)")
{
    ImOkTest::Harness harness{};
    float col[3]{ 1.0f, 0.5f, 0.25f };
    float kelvin{ 2700.0f };
    float intensity{ 800.0f };
    ImOkTest::ItemRect edit{};
    auto ui = [&]()
    {
        ImOk::LightEdit("##light", ImOkLightKind_Point, col, ImOkStoredAs_LinearSrgb, &kelvin, &intensity,
                        ImOkLightUnit_Lumen);
        edit = ImOkTest::LastItemRect();
    };
    harness.Settle(ui);

    harness.RightClick(ui, ImOkTest::LightSwatchCenter(edit));
    CHECK_FALSE(ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel));
}

// --- Swatch tooltip --------------------------------------------------------------
//
// What a frame showed, read through ImGui's log: LogToClipboard captures every rendered text,
// tooltips included, and LogFinish hands it to the clipboard, which the harness keeps.

TEST_CASE("Swatch tooltip: ColorEdit3 names col's space, hex and oklch(); NoTooltip turns it off")
{
    ImOkTest::Harness harness{};
    float col[3]{ 204.0f / 255.0f, 76.0f / 255.0f, 51.0f / 255.0f };
    ImOkColorEditFlags flags{ 0 };
    ImOkTest::ItemRect edit{};
    auto ui = [&]()
    {
        ImGui::LogToClipboard();
        ImOk::ColorEdit3("##edit", col, ImOkStoredAs_EncodedSrgb, flags);
        edit = ImOkTest::LastItemRect();
        ImGui::LogFinish();
    };
    harness.Settle(ui);

    harness.Hover(ui, ImOkTest::SwatchCenter(edit), 3); // Before the hover delay
    CHECK(harness.Clipboard().find("(stored)") == std::string::npos);

    harness.Hover(ui, ImOkTest::SwatchCenter(edit));
    const std::string shown{ harness.Clipboard() };
    CAPTURE(shown);
    CHECK(shown.find("Encoded sRGB (stored): 0.8000, 0.2980, 0.2000") != std::string::npos);
    CHECK(shown.find("#CC4C33") != std::string::npos);
    CHECK(shown.find("oklch(58.60% 0.16761 33.00)") != std::string::npos);

    harness.Settle(ui);
    flags = ImOkColorEditFlags_NoTooltip;
    harness.Hover(ui, ImOkTest::SwatchCenter(edit));
    CHECK(harness.Clipboard().find("(stored)") == std::string::npos);
}

TEST_CASE("Swatch tooltip: NoBrightness shows col as stored, not raised")
{
    ImOkTest::Harness harness{};
    float col[4]{ 0.2f, 0.1f, 0.05f, 0.5f }; // Darker than largest channel 1: the swatch shows it raised
    ImOkTest::ItemRect edit{};
    auto ui = [&]()
    {
        ImGui::LogToClipboard();
        ImOk::ColorEdit4("##edit", col, ImOkStoredAs_LinearSrgb, ImOkColorEditFlags_NoBrightness);
        edit = ImOkTest::LastItemRect();
        ImGui::LogFinish();
    };
    harness.Settle(ui);
    harness.Hover(ui, ImOkTest::SwatchCenter(edit));
    const std::string shown{ harness.Clipboard() };
    CAPTURE(shown);
    CHECK(shown.find("Linear sRGB (stored): 0.2000, 0.1000, 0.0500, 0.5000") != std::string::npos);
}

TEST_CASE("Swatch tooltip: LightEdit's row tells what passes, never floats")
{
    ImOkTest::Harness harness{};
    float col[3]{ 1.0f, 0.0f, 0.0f };
    float kelvin{ 2700.0f };
    float intensity{ 800.0f };
    ImOkLightEditFlags flags{ 0 };
    ImOkTest::ItemRect edit{};
    auto ui = [&]()
    {
        ImGui::LogToClipboard();
        ImOk::LightEdit("##light", ImOkLightKind_Point, col, ImOkStoredAs_LinearSrgb, &kelvin, &intensity,
                        ImOkLightUnit_Lumen, flags);
        edit = ImOkTest::LastItemRect();
        ImGui::LogFinish();
    };
    harness.Settle(ui);

    harness.Hover(ui, ImOkTest::LightSwatchCenter(edit));
    const std::string shown{ harness.Clipboard() };
    CAPTURE(shown);
    CHECK(shown.find("Color x 2700 K, brightness not shown") != std::string::npos);
    CHECK(shown.find("The color passes ") != std::string::npos);
    CHECK(shown.find("(stored)") == std::string::npos);

    harness.Settle(ui);
    flags = ImOkLightEditFlags_NoTooltip;
    harness.Hover(ui, ImOkTest::LightSwatchCenter(edit));
    CHECK(harness.Clipboard().find("The color passes ") == std::string::npos);
}

// --- Picker popup ------------------------------------------------------------------

TEST_CASE("Picker popup: opens under the swatch, as ImGui's ColorEdit4, and stays")
{
    ImOkTest::Harness harness{};
    float col[3]{ 0.8f, 0.3f, 0.2f };
    ImOkTest::ItemRect edit{};
    auto ui = [&]()
    {
        ImOk::ColorEdit3("##edit", col, ImOkStoredAs_EncodedSrgb);
        edit = ImOkTest::LastItemRect();
    };
    harness.Settle(ui);

    // Clicked near the swatch's corner: at the mouse it would open here, not under the swatch
    const float frame{ ImGui::GetFrameHeight() };
    const ImVec2 swatch{ ImOkTest::SwatchCenter(edit) };
    const ImVec2 corner{ swatch.x + frame * 0.4f, swatch.y + frame * 0.4f };
    harness.Hover(ui, corner); // The swatch's tooltip open on the click frame, as a user's would be
    harness.Click(ui, corner);
    harness.Settle(ui);

    const ImOkTest::ItemRect popup{ harness.PopupRect() };
    // MIRRORS imok.cpp ColorEditImpl: the swatch is the item's last frame-height square
    CHECK(popup.min.x == doctest::Approx(swatch.x - frame * 0.5f));
    CHECK(popup.min.y == doctest::Approx(edit.min.y + frame + ImGui::GetStyle().ItemSpacing.y));

    harness.Hover(ui, ImVec2(popup.min.x + 40.0f, popup.min.y + 60.0f)); // Later frames: placed once
    CHECK(harness.PopupRect().min.x == popup.min.x);
    CHECK(harness.PopupRect().min.y == popup.min.y);
}

TEST_CASE("Picker popup: LightEdit's opens under its swatch, past the tooltips opened in between")
{
    ImOkTest::Harness harness{};
    float col[3]{ 1.0f, 0.5f, 0.25f };
    float kelvin{ 2700.0f };
    float intensity{ 800.0f };
    ImOkTest::ItemRect edit{};
    auto ui = [&]()
    {
        ImOk::LightEdit("##light", ImOkLightKind_Point, col, ImOkStoredAs_LinearSrgb, &kelvin, &intensity,
                        ImOkLightUnit_Lumen);
        edit = ImOkTest::LastItemRect();
    };
    harness.Settle(ui);

    const ImVec2 swatch{ ImOkTest::LightSwatchCenter(edit) };
    harness.Hover(ui, swatch); // The swatch's tooltip opens a window before BeginPopup
    harness.Click(ui, swatch);
    harness.Settle(ui);

    const ImOkTest::ItemRect popup{ harness.PopupRect() };
    // MIRRORS imok.cpp LightEditImpl: the swatch is the row's first frame-height square
    CHECK(popup.min.x == doctest::Approx(edit.min.x));
    CHECK(popup.min.y == doctest::Approx(edit.min.y + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y));
}

// --- style.Alpha -----------------------------------------------------------------

namespace
{
    // The most opaque alpha byte among the vertices added since start
    int MaxVertexAlpha(const ImDrawList& drawList, int start)
    {
        int result{ 0 };
        for (int i{ start }; i < drawList.VtxBuffer.Size; ++i)
        {
            const int alpha{ static_cast<int>((drawList.VtxBuffer[i].col >> IM_COL32_A_SHIFT) & 0xFF) };
            result = (alpha > result) ? alpha : result;
        }
        return result;
    }
}

TEST_CASE("style.Alpha: the drawing helpers multiply it in, as ImGui::GetColorU32")
{
    ImOkTest::Harness harness{};
    using Helper = void (*)(ImDrawList*);
    static const ImVec2 MIN{ 10.0f, 10.0f };
    static const ImVec2 MAX{ 110.0f, 40.0f };
    const Helper helpers[]{
        [](ImDrawList* d) { ImOk::AddRectGradientLinearSrgb(d, MIN, MAX, ImOk::LinearSrgb{ 1.0f, 0.0f, 0.0f }, ImOk::LinearSrgb{ 0.0f, 0.0f, 1.0f }); },
        [](ImDrawList* d) { ImOk::AddRectGradientOkLab(d, MIN, MAX, ImOk::OkLab{ 0.6f, 0.2f, 0.1f }, ImOk::OkLab{ 0.5f, -0.1f, -0.2f }); },
        [](ImDrawList* d) { ImOk::AddRectGradientOkLCh(d, MIN, MAX, ImOk::OkLCh{ 0.6f, 0.1f, 30.0f }, ImOk::OkLCh{ 0.6f, 0.1f, 260.0f }, ImOk::HueDirection::Shorter); },
        [](ImDrawList* d) { ImOk::AddRectOkhsvSaturationValue(d, MIN, MAX, 30.0f); },
        [](ImDrawList* d) { ImOk::AddRectOkhslSaturationLightness(d, MIN, MAX, 30.0f); },
        [](ImDrawList* d) { ImOk::AddRectOkhsvHueSaturation(d, MIN, MAX); },
        [](ImDrawList* d) { ImOk::AddRectHueBar(d, MIN, MAX); },
        [](ImDrawList* d) { ImOk::AddRectAlphaBar(d, MIN, MAX, ImOk::LinearSrgb{ 1.0f, 0.5f, 0.25f }); },
    };
    constexpr int COUNT{ static_cast<int>(sizeof(helpers) / sizeof(helpers[0])) };
    int maxAtOne[COUNT]{};
    int maxAtHalf[COUNT]{};
    auto ui = [&]()
    {
        ImDrawList* drawList{ ImGui::GetWindowDrawList() };
        for (int i{ 0 }; i < COUNT; ++i)
        {
            const int start{ drawList->VtxBuffer.Size };
            helpers[i](drawList);
            maxAtOne[i] = MaxVertexAlpha(*drawList, start);

            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
            const int half{ drawList->VtxBuffer.Size };
            helpers[i](drawList);
            maxAtHalf[i] = MaxVertexAlpha(*drawList, half);
            ImGui::PopStyleVar();
        }
    };
    harness.Settle(ui);

    for (int i{ 0 }; i < COUNT; ++i)
    {
        CAPTURE(i);
        CHECK(maxAtOne[i] == 255);
        CHECK(maxAtHalf[i] == 128); // 0.5 x 255 rounded; the alpha bar's checkerboard, through GetColorU32, 127
    }
}

TEST_CASE("style.Alpha: nothing a disabled widget draws is more opaque than ImGui's disabled items")
{
    ImOkTest::Harness harness{};
    float col3[3]{ 0.8f, 0.3f, 0.2f };
    float col4[4]{ 0.8f, 0.3f, 0.2f, 0.5f };
    ImOk::Okhsv hsv{ 30.0f, 0.8f, 0.9f };
    ImOk::Okhsl hsl{ 30.0f, 0.8f, 0.6f };
    float alpha{ 0.5f };
    float kelvin{ 2700.0f };
    float intensity{ 800.0f };
    constexpr int WIDGET_COUNT{ 11 };
    int which{ 0 };
    int maxAlpha{ 0 };
    auto ui = [&]()
    {
        ImDrawList* drawList{ ImGui::GetWindowDrawList() };
        ImGui::PushID(which);
        ImGui::BeginDisabled();
        const int start{ drawList->VtxBuffer.Size };
        switch (which)
        {
        case 0: ImOk::ColorPicker3("##w", col3, ImOkStoredAs_LinearSrgb); break;
        case 1: ImOk::ColorPicker4("##w", col4, ImOkStoredAs_LinearSrgb); break;
        case 2: ImOk::ColorPicker3("##w", col3, ImOkStoredAs_LinearSrgb, ImOkColorEditFlags_NoBrightness); break;
        case 3: ImOk::ColorPicker3("##w", col3, ImOkStoredAs_LinearSrgb, ImOkColorEditFlags_PickerOkhsl); break;
        case 4: ImOk::ColorPickerOkhsv("##w", &hsv, &alpha); break;
        case 5: ImOk::ColorPickerOkhsl("##w", &hsl, &alpha); break;
        case 6: ImOk::ColorEdit4("##w", col4, ImOkStoredAs_LinearSrgb); break;
        case 7: ImOk::TemperatureEdit("##w", &kelvin); break;
        case 8: ImOk::IntensityEdit("##w", &intensity, ImOkLightUnit_Lumen); break;
        case 9: ImOk::LightEdit("##w", ImOkLightKind_Point, col3, ImOkStoredAs_LinearSrgb, &kelvin, &intensity, ImOkLightUnit_Lumen); break;
        case 10: ImOk::LightPicker("##w", ImOkLightKind_Point, col3, ImOkStoredAs_LinearSrgb, &kelvin, &intensity, ImOkLightUnit_Lumen); break;
        default: break;
        }
        maxAlpha = MaxVertexAlpha(*drawList, start);
        ImGui::EndDisabled();
        ImGui::PopID();
    };

    // ImGui's own disabled items reach exactly this (DisabledAlpha 0.6: 153)
    const int bound{ static_cast<int>(ImGui::GetStyle().DisabledAlpha * 255.0f + 0.5f) };
    for (which = 0; which < WIDGET_COUNT; ++which)
    {
        harness.Settle(ui);
        CAPTURE(which);
        CHECK(maxAlpha > 0); // It drew something
        CHECK(maxAlpha <= bound);
    }
}

// --- NoInputs and NoOptions --------------------------------------------------------

TEST_CASE("NoInputs: the swatch alone, which opens the popup, copies, and takes drops")
{
    ImOkTest::Harness harness{};
    float col[3]{ 0.2f, 0.1f, 0.05f };
    float source[3]{ 204.0f / 255.0f, 76.0f / 255.0f, 51.0f / 255.0f };
    ImOkTest::ItemRect swatch{};
    ImOkTest::ItemRect sourceEdit{};
    auto ui = [&]()
    {
        ImOk::ColorEdit3("##swatch", col, ImOkStoredAs_LinearSrgb, ImOkColorEditFlags_NoInputs);
        swatch = ImOkTest::LastItemRect();
        ImOk::ColorEdit3("##source", source, ImOkStoredAs_EncodedSrgb);
        sourceEdit = ImOkTest::LastItemRect();
    };
    harness.Settle(ui);

    // One frame-height square, whatever the item width (ITEM_WIDTH is pushed)
    const float frame{ ImGui::GetFrameHeight() };
    CHECK(swatch.max.x - swatch.min.x == doctest::Approx(frame));

    // The menu keeps Copy as (here after the Picker section): row 0 is the stored floats
    REQUIRE(harness.CopyFromMenu(ui, swatch.Center(), 0));
    CHECK(harness.Clipboard() == "(0.2f, 0.1f, 0.05f)");
    harness.Settle(ui);

    harness.Click(ui, swatch.Center());
    harness.Settle(ui);
    CHECK(harness.PopupRect().max.x > harness.PopupRect().min.x);
    harness.Click(ui, ImVec2(1150.0f, 1150.0f)); // Outside: closes it
    harness.Settle(ui);

    // A drop on the swatch: encoded into linear storage, decoded
    harness.Drag(ui, ImOkTest::SwatchCenter(sourceEdit), swatch.Center());
    const ImOk::LinearSrgb expected{ ImOk::EncodedSrgbToLinearSrgb({ source[0], source[1], source[2] }) };
    CHECK(col[0] == doctest::Approx(expected.r));
    CHECK(col[1] == doctest::Approx(expected.g));
    CHECK(col[2] == doctest::Approx(expected.b));
}

TEST_CASE("NoOptions: no right-click menu on ColorEdit or ColorPicker")
{
    ImOkTest::Harness harness{};
    float editCol[3]{ 0.8f, 0.3f, 0.2f };
    float pickerCol[3]{ 0.8f, 0.3f, 0.2f };
    ImOkTest::ItemRect edit{};
    ImOkTest::ItemRect picker{};
    auto ui = [&]()
    {
        ImOk::ColorEdit3("##edit", editCol, ImOkStoredAs_EncodedSrgb, ImOkColorEditFlags_NoOptions);
        edit = ImOkTest::LastItemRect();
        ImOk::ColorPicker3("##picker", pickerCol, ImOkStoredAs_EncodedSrgb, ImOkColorEditFlags_NoOptions);
        picker = ImOkTest::LastItemRect();
    };
    harness.Settle(ui);

    const ImGuiPopupFlags anyPopup{ ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel };
    harness.RightClick(ui, edit.Left());
    CHECK_FALSE(ImGui::IsPopupOpen(nullptr, anyPopup));
    harness.RightClick(ui, picker.Center());
    CHECK_FALSE(ImGui::IsPopupOpen(nullptr, anyPopup));
}