#include <doctest/doctest.h>
#include <cstring>
#include "imok_color_internal.h"

#include <initializer_list>

using ImOk::Internal::HexForms;

namespace
{
    // Parses into sentinels, so "writes nothing on failure" is visible
    struct Parsed
    {
        bool ok{ false };
        ImOk::EncodedSrgb encoded{ -1.0f, -1.0f, -1.0f };
        float alpha{ -1.0f };
    };

    Parsed Parse(const char* text, bool withAlpha = true, HexForms forms = HexForms::All)
    {
        Parsed parsed{};
        parsed.ok = ImOk::Internal::HexToEncodedSrgb(text, &parsed.encoded, withAlpha ? &parsed.alpha : nullptr, forms);
        return parsed;
    }

    float Byte(int k)
    {
        return static_cast<float>(k) / 255.0f;
    }
}

TEST_CASE("Hex: six and eight digits, '#' optional, any case, spaces around")
{
    for (const char* text : { "#CC4D33", "cc4d33", "  #Cc4D33\t" })
    {
        const Parsed parsed{ Parse(text) };
        CHECK(parsed.ok);
        CHECK(parsed.encoded.r == Byte(0xCC));
        CHECK(parsed.encoded.g == Byte(0x4D));
        CHECK(parsed.encoded.b == Byte(0x33));
        CHECK(parsed.alpha == 1.0f);
    }
    CHECK(Parse("#CC4D3380").alpha == Byte(0x80));
}

TEST_CASE("Hex: short forms repeat each digit")
{
    const Parsed shortForm{ Parse("#123") };
    const Parsed longForm{ Parse("#112233") };
    CHECK(shortForm.ok);
    CHECK(shortForm.encoded.r == longForm.encoded.r);
    CHECK(shortForm.encoded.g == longForm.encoded.g);
    CHECK(shortForm.encoded.b == longForm.encoded.b);
    CHECK(Parse("#1234").alpha == Byte(0x44));

    ImOk::EncodedSrgb publicParsed{};
    CHECK(ImOk::HexToEncodedSrgb("#abc", &publicParsed));
    CHECK(publicParsed.r == Byte(0xAA));
}

TEST_CASE("Hex: AA without an alpha pointer fails and writes nothing")
{
    for (const char* text : { "#11223344", "#1234" })
    {
        const Parsed parsed{ Parse(text, false) };
        CHECK_FALSE(parsed.ok);
        CHECK(parsed.encoded.r == -1.0f);
    }
    CHECK(Parse("#112233", false).ok);
}

TEST_CASE("Hex: invalid text fails and writes nothing")
{
    for (const char* text : { "", "#", "12", "12345", "1234567", "123456789", "#GG0000",
                              "##112233", "11 2233", "#112233x", "0x112233" })
    {
        const Parsed parsed{ Parse(text) };
        CHECK_FALSE(parsed.ok);
        CHECK(parsed.encoded.r == -1.0f);
        CHECK(parsed.alpha == -1.0f);
    }
    ImOk::EncodedSrgb encoded{};
    CHECK_FALSE(ImOk::HexToEncodedSrgb(nullptr, &encoded));
}

TEST_CASE("Hex: LongOnly rejects the short forms")
{
    CHECK_FALSE(Parse("#ABC", true, HexForms::LongOnly).ok);
    CHECK_FALSE(Parse("#ABCD", true, HexForms::LongOnly).ok);
    CHECK(Parse("#AABBCC", true, HexForms::LongOnly).ok);
    CHECK(Parse("#AABBCCDD", true, HexForms::LongOnly).ok);
}

TEST_CASE("Hex formatting: uppercase, rounded, clamped")
{
    char out[ImOk::HEX_BUFFER_SIZE]{};
    ImOk::EncodedSrgbToHex({ 1.5f, -0.2f, 0.5f }, out);
    CHECK(std::strcmp(out, "#FF0080") == 0);

    const float alpha{ 0.25f };
    ImOk::EncodedSrgbToHex({ 1.5f, -0.2f, 0.5f }, out, &alpha);
    CHECK(std::strcmp(out, "#FF008040") == 0);
}

TEST_CASE("Hex: every byte survives format then parse, bit-exact")
{
    for (int k{ 0 }; k <= 255; ++k)
    {
        const ImOk::EncodedSrgb encoded{ Byte(k), Byte(255 - k), Byte(k / 2) };
        const float alpha{ Byte(k) };
        char out[ImOk::HEX_BUFFER_SIZE]{};
        ImOk::EncodedSrgbToHex(encoded, out, &alpha);

        const Parsed parsed{ Parse(out) };
        REQUIRE(parsed.ok);
        REQUIRE(parsed.encoded.r == encoded.r);
        REQUIRE(parsed.encoded.g == encoded.g);
        REQUIRE(parsed.encoded.b == encoded.b);
        REQUIRE(parsed.alpha == alpha);
    }
}