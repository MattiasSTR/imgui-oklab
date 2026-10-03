#include <doctest/doctest.h>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include "imok_internal.h"

using ImOk::Internal::Surface;
using ImOk::Internal::SurfaceValue;

namespace
{
    constexpr Surface OKHSV{ Surface::OkhsvSaturationValue };
    constexpr Surface OKHSL{ Surface::OkhslSaturationLightness };

    // Every 5 degrees, s and the square's other value in tenths (that value above 0: black
    // has no saturation to compare)
    template <typename Visit>
    void ForEachSurfaceValue(const Visit& visit)
    {
        for (int h{ 0 }; h < 360; h += 5)
        {
            for (int s{ 0 }; s <= 10; ++s)
            {
                for (int y{ 1 }; y <= 10; ++y)
                {
                    visit(SurfaceValue{ static_cast<float>(h), static_cast<float>(s) / 10.0f, static_cast<float>(y) / 10.0f });
                }
            }
        }
    }
}

TEST_CASE("Converting to the same surface returns the value bit-exact")
{
    const SurfaceValue value{ 123.456f, 0.321f, 0.654f };
    for (const Surface surface : { OKHSV, OKHSL })
    {
        const SurfaceValue converted{ ImOk::Internal::ConvertSurfaceValue(surface, surface, value) };
        CHECK(converted.h == value.h);
        CHECK(converted.x == value.x);
        CHECK(converted.y == value.y);
    }
}

TEST_CASE("Converting between surfaces keeps the hue exactly, at grays and black too")
{
    // The conversion alone would give a gray or black hue 0
    const SurfaceValue gray{ ImOk::Internal::ConvertSurfaceValue(OKHSV, OKHSL, { 200.0f, 0.0f, 0.5f }) };
    CHECK(gray.h == 200.0f);
    CHECK(gray.x == 0.0f);

    const SurfaceValue black{ ImOk::Internal::ConvertSurfaceValue(OKHSL, OKHSV, { 200.0f, 0.7f, 0.0f }) };
    CHECK(black.h == 200.0f);
    CHECK(black.y == 0.0f);

    ForEachSurfaceValue([](const SurfaceValue& value)
    {
        CHECK(ImOk::Internal::ConvertSurfaceValue(OKHSV, OKHSL, value).h == value.h);
    });
}

TEST_CASE("Okhsv to Okhsl and back: the same color, within float rounding")
{
    // Measured: s within 2.6e-5 (near s = 0.9 at v = 1), v within 1e-6
    float worstS{ 0.0f };
    float worstV{ 0.0f };
    ForEachSurfaceValue([&](const SurfaceValue& value)
    {
        const SurfaceValue there{ ImOk::Internal::ConvertSurfaceValue(OKHSV, OKHSL, value) };
        const SurfaceValue back{ ImOk::Internal::ConvertSurfaceValue(OKHSL, OKHSV, there) };
        worstS = std::fmax(worstS, std::fabs(back.x - value.x));
        worstV = std::fmax(worstV, std::fabs(back.y - value.y));
    });
    CHECK(worstS < 3e-5f);
    CHECK(worstV < 2e-6f);
}

TEST_CASE("A surface value to Oklab and back: the same color")
{
    // Compared in Oklab, where every value is defined (s at grays is not)
    float worst{ 0.0f };
    for (const Surface surface : { OKHSV, OKHSL })
    {
        ForEachSurfaceValue([&](const SurfaceValue& value)
        {
            const ImOk::OkLab lab{ ImOk::Internal::SurfaceValueToOkLab(surface, value) };
            const ImOk::OkLab back{ ImOk::Internal::SurfaceValueToOkLab(surface, ImOk::Internal::OkLabToSurfaceValue(surface, lab)) };
            worst = std::fmax(worst, std::fmax(std::fabs(back.L - lab.L),
                                     std::fmax(std::fabs(back.a - lab.a), std::fabs(back.b - lab.b))));
        });
    }
    CHECK(worst < 2e-6f);
}

TEST_CASE("Switching surfaces at black keeps the saturation, both ways")
{
    const SurfaceValue onOkhsv{ ImOk::Internal::ConvertSurfaceValue(OKHSL, OKHSV, { 200.0f, 0.7f, 0.0f }) };
    CHECK(onOkhsv.x == 0.7f);
    CHECK(onOkhsv.y == 0.0f);

    const SurfaceValue backOnOkhsl{ ImOk::Internal::ConvertSurfaceValue(OKHSV, OKHSL, onOkhsv) };
    CHECK(backOnOkhsl.h == 200.0f);
    CHECK(backOnOkhsl.x == 0.7f);
    CHECK(backOnOkhsl.y == 0.0f);
}

TEST_CASE("Keeping follows each surface's rule at white")
{
    const ImOk::OkLab white{ 1.0f, 0.0f, 0.0f };
    const SurfaceValue current{ 200.0f, 0.7f, 0.4f };

    const SurfaceValue onOkhsv{ ImOk::Internal::OkLabToSurfaceValueKeeping(OKHSV, white, current) };
    CHECK(onOkhsv.h == 200.0f);
    CHECK(onOkhsv.x == 0.0f);

    const SurfaceValue onOkhsl{ ImOk::Internal::OkLabToSurfaceValueKeeping(OKHSL, white, current) };
    CHECK(onOkhsl.h == 200.0f);
    CHECK(onOkhsl.x == 0.7f);
    CHECK(onOkhsl.y == 1.0f);
}

TEST_CASE("A dropped pure blue shows as itself on both surfaces, in both storages")
{
    // A drop's path: col, then the surface value, then swatch, marker and fields on both surfaces.
    // At blue's corner #0000FB once showed 69 steps off, with s 1.087 and the marker off the square.
    const SurfaceValue current{ 30.0f, 0.5f, 0.5f };
    const ImOkStoredAs storages[]{ ImOkStoredAs_EncodedSrgb, ImOkStoredAs_LinearSrgb };
    for (const ImOkStoredAs storage : storages)
    {
        for (const Surface surface : { OKHSV, OKHSL })
        {
            const Surface other{ (surface == OKHSV) ? OKHSL : OKHSV };
            const int surfaceIndex{ static_cast<int>(surface) };
            CAPTURE(storage);
            CAPTURE(surfaceIndex);

            for (int byte{ 1 }; byte <= 255; ++byte)
            {
                CAPTURE(byte);
                const float payload[3]{ 0.0f, 0.0f, static_cast<float>(byte) / 255.0f };
                float col[3]{};
                ImOk::Internal::ApplyColorPayload(payload, 3, storage, col, nullptr);

                const SurfaceValue value{ ImOk::Internal::OkLabToSurfaceValueKeeping(
                    surface, ImOk::LinearSrgbToOkLab(ImOk::Internal::StoredToLinearSrgb(col, storage)), current) };
                CHECK(value.x <= 1.0f + 1e-3f); // The marker stays on the square

                const SurfaceValue inOther{ ImOk::Internal::ConvertSurfaceValue(surface, other, value) };
                const ImOk::OkLab shownLabs[2]{ ImOk::Internal::SurfaceValueToOkLab(surface, value),
                                                ImOk::Internal::SurfaceValueToOkLab(other, inOther) };
                for (const ImOk::OkLab& lab : shownLabs)
                {
                    const ImOk::EncodedSrgb shown{ ImOk::LinearSrgbToEncodedSrgb(ImOk::OkLabToLinearSrgb(lab)) };
                    CHECK(ImOk::Internal::ChannelToByte(shown.r) <= 1);
                    CHECK(ImOk::Internal::ChannelToByte(shown.g) <= 1);
                    CHECK(std::abs(ImOk::Internal::ChannelToByte(shown.b) - byte) <= 1);
                }
            }
        }
    }
}