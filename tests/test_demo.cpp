#include <doctest/doctest.h>
#include <cstring>
#include "imok.h"
#include "widget_harness.h"

// Runs ImOk::ShowDemoWindow with every section open, so all of its code runs under ImGui's
// checks: a PushID/PopID or TreeNode/TreePop mismatch, or a misused ImOk flag, asserts.
// (ID conflicts are not caught: ImGui only detects those under the mouse.)

namespace
{
    // MIRRORS imok_demo.cpp: the collapsing headers, and the tree nodes inside "Good practices"
    // (in its PushID("practices") scope). A renamed label would silently stay closed, so each one
    // is checked to add content when opened.
    const char* const HEADERS[]{
        "Good practices: decode once, work, encode once",
        "1. Storage: what your floats hold",
        "2. Picker: how the artist moves through colors",
        "3. Display: which numbers the fields show",
        "4. Alpha: stored as-is",
        "5. Native perceptual widgets",
        "6. Lights and emission: color, temperature, intensity",
        "7. Drag and drop: same color, new numbers",
        "8. Compared with ImGui's picker",
    };
    const char* const PRACTICES_HEADER{ "Good practices: decode once, work, encode once" };
    const char* const PRACTICES_ID{ "practices" };
    const char* const PRACTICES[]{
        "Entering: decode once",
        "Physics: linear sRGB",
        "Perception: Oklab",
        "Leaving: encode once",
        "Picking: Okhsv or Okhsl",
    };

    // Which sections are open: set in the demo window's storage before it is drawn, which is
    // where CollapsingHeader and TreeNode keep their open state
    struct Sections
    {
        bool headers[sizeof(HEADERS) / sizeof(HEADERS[0])]{};
        bool practices[sizeof(PRACTICES) / sizeof(PRACTICES[0])]{};
    };

    void OpenSections(const Sections& open)
    {
        ImGui::Begin("ImOk Demo"); // Appends to the demo's window: same storage and ID scope
        ImGuiStorage* storage{ ImGui::GetStateStorage() };
        for (size_t i{ 0 }; i < sizeof(HEADERS) / sizeof(HEADERS[0]); ++i)
        {
            storage->SetInt(ImGui::GetID(HEADERS[i]), open.headers[i] ? 1 : 0);
        }
        ImGui::PushID(PRACTICES_ID);
        for (size_t i{ 0 }; i < sizeof(PRACTICES) / sizeof(PRACTICES[0]); ++i)
        {
            storage->SetInt(ImGui::GetID(PRACTICES[i]), open.practices[i] ? 1 : 0);
        }
        ImGui::PopID();
        ImGui::End();
    }

    // Height of the demo window's content with these sections open (read after the frames)
    float ContentHeight(ImOkTest::Harness& harness, const Sections& open)
    {
        const auto ui{ [&]
        {
            OpenSections(open);
            ImOk::ShowDemoWindow();
        } };
        harness.Settle(ui, 3);
        const ImGuiWindow* demo{ ImGui::FindWindowByName("ImOk Demo") };
        REQUIRE(demo != nullptr);
        return demo->ContentSize.y;
    }

    int IndexOfPracticesHeader()
    {
        for (size_t i{ 0 }; i < sizeof(HEADERS) / sizeof(HEADERS[0]); ++i)
        {
            if (std::strcmp(HEADERS[i], PRACTICES_HEADER) == 0)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
}

TEST_CASE("The demo runs with every section open")
{
    ImOkTest::Harness harness{};
    const Sections closed{};

    Sections all{};
    for (bool& header : all.headers)
    {
        header = true;
    }
    for (bool& practice : all.practices)
    {
        practice = true;
    }
    const float allHeight{ ContentHeight(harness, all) };
    const float closedHeight{ ContentHeight(harness, closed) };
    CHECK(allHeight > closedHeight);

    // Each label must match the demo: opening it alone adds content
    for (size_t i{ 0 }; i < sizeof(HEADERS) / sizeof(HEADERS[0]); ++i)
    {
        Sections one{};
        one.headers[i] = true;
        CAPTURE(HEADERS[i]);
        CHECK(ContentHeight(harness, one) > closedHeight);
    }

    const int practicesIndex{ IndexOfPracticesHeader() };
    REQUIRE(practicesIndex >= 0);
    Sections practicesOnly{};
    practicesOnly.headers[practicesIndex] = true;
    const float practicesHeight{ ContentHeight(harness, practicesOnly) };
    for (size_t i{ 0 }; i < sizeof(PRACTICES) / sizeof(PRACTICES[0]); ++i)
    {
        Sections one{ practicesOnly };
        one.practices[i] = true;
        CAPTURE(PRACTICES[i]);
        CHECK(ContentHeight(harness, one) > practicesHeight);
    }
}