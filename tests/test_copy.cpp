#include <doctest/doctest.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include "imok_internal.h"

using ImOk::Internal::CopyFormat;
using ImOk::Internal::CopyText;
using ImOk::Internal::COPY_BUFFER_SIZE;

namespace
{
    // Byte k as hex parses it: divided, not multiplied by 1/255
    float Byte(int k)
    {
        return static_cast<float>(k) / 255.0f;
    }

    std::string Literal(float x)
    {
        char out[ImOk::Internal::FLOAT_LITERAL_BUFFER_SIZE]{};
        ImOk::Internal::FloatToLiteral(x, out, sizeof(out));
        return out;
    }

    // A literal is a float literal, and strtof reads it back bit-exact
    bool ReadsBackExactly(float x)
    {
        const std::string literal{ Literal(x) };
        if (literal.empty() || literal.back() != 'f' || literal.find_first_of(".e") == std::string::npos)
        {
            return false;
        }
        const float back{ std::strtof(literal.c_str(), nullptr) }; // Stops at the 'f'
        return std::memcmp(&back, &x, sizeof(float)) == 0;
    }

    std::string Copy(CopyFormat format, const float col[3], const float* alpha, ImOkStoredAs storage)
    {
        char out[COPY_BUFFER_SIZE]{};
        CopyText(format, col, alpha, storage, out);
        return out;
    }

    // A float row reads back bit-exact: "(a, b, c)" or "(a, b, c, d)", each a float literal
    bool ReadsBack(const std::string& text, const float* expected, int count)
    {
        const char* p{ text.c_str() };
        if (*p++ != '(')
        {
            return false;
        }
        for (int i{ 0 }; i < count; ++i)
        {
            char* end{ nullptr };
            const float value{ std::strtof(p, &end) };
            if (end == p || *end != 'f' || std::memcmp(&value, &expected[i], sizeof(float)) != 0)
            {
                return false;
            }
            p = end + 1;
            const char* separator{ (i + 1 < count) ? ", " : ")" };
            if (std::strncmp(p, separator, std::strlen(separator)) != 0)
            {
                return false;
            }
            p += std::strlen(separator);
        }
        return *p == '\0';
    }
    
    const float CC4C33_ENCODED[3]{ Byte(0xCC), Byte(0x4C), Byte(0x33) };

    ImOk::LinearSrgb Cc4c33Linear()
    {
        return ImOk::EncodedSrgbToLinearSrgb({ CC4C33_ENCODED[0], CC4C33_ENCODED[1], CC4C33_ENCODED[2] });
    }

    // Test-only parser for OkLChToCssText's own output without alpha: "oklch(L% C h)" or
    // "oklch(L% 0 none)". strtof, not sscanf, which MSVC deprecates (C4996).
    bool ParseOkLCh(const char* text, ImOk::OkLCh* lch)
    {
        constexpr char PREFIX[]{ "oklch(" };
        if (std::strncmp(text, PREFIX, sizeof(PREFIX) - 1) != 0)
        {
            return false;
        }
        const char* p{ text + sizeof(PREFIX) - 1 };
        char* end{ nullptr };

        const float L{ std::strtof(p, &end) };
        if (end == p || *end != '%')
        {
            return false;
        }
        p = end + 1;

        const float C{ std::strtof(p, &end) };
        if (end == p || *end != ' ')
        {
            return false;
        }
        p = end + 1;

        float h{ 0.0f };
        if (std::strncmp(p, "none", 4) == 0)
        {
            if (C != 0.0f)
            {
                return false;
            }
            p += 4;
        }
        else
        {
            h = std::strtof(p, &end);
            if (end == p)
            {
                return false;
            }
            p = end;
        }
        if (std::strcmp(p, ")") != 0)
        {
            return false;
        }

        *lch = { L / 100.0f, C, h };
        return true;
    }
}

TEST_CASE("Copy: float literals")
{
    CHECK(Literal(0.8f) == "0.8f");
    CHECK(Literal(0.5f) == "0.5f");
    CHECK(Literal(1.0f) == "1.0f"); // Not "1f", which doesn't compile
    CHECK(Literal(0.0f) == "0.0f");
    CHECK(Literal(-0.0f) == "-0.0f");
    CHECK(Literal(12.0f) == "12.0f");
    CHECK(Literal(1e-5f) == "1e-05f"); // An exponent makes it a float literal: no point needed
    CHECK(ReadsBackExactly(1e-5f));
    CHECK(ReadsBackExactly(-3.4e38f));
    CHECK(ReadsBackExactly(1e-40f)); // Denormal
}

TEST_CASE("Copy: every byte's encoded and linear float reads back bit-exact")
{
    for (int k{ 0 }; k <= 255; ++k)
    {
        const float encoded{ Byte(k) };
        const float linear{ ImOk::EncodedSrgbToLinearSrgb({ encoded, encoded, encoded }).r };
        REQUIRE(ReadsBackExactly(encoded));
        REQUIRE(ReadsBackExactly(linear));
    }
}

TEST_CASE("Copy: #CC4C33, every row, both storages")
{
    // Exact strings only where no powf is involved: linear floats depend on the CRT's powf
    // (glibc's is correctly rounded, MSVC's need not be), so float rows that cross the transfer
    // function are read back and compared with the conversion instead.
    const ImOk::LinearSrgb linear{ Cc4c33Linear() };
    const float linearCol[3]{ linear.r, linear.g, linear.b };
    const ImOk::EncodedSrgb reencoded{ ImOk::LinearSrgbToEncodedSrgb(linear) };

    CHECK(Copy(CopyFormat::StoredFloats, CC4C33_ENCODED, nullptr, ImOkStoredAs_EncodedSrgb) == "(0.8f, 0.29803923f, 0.2f)");
    CHECK(ReadsBack(Copy(CopyFormat::OtherFloats, CC4C33_ENCODED, nullptr, ImOkStoredAs_EncodedSrgb), linearCol, 3));
    CHECK(ReadsBack(Copy(CopyFormat::StoredFloats, linearCol, nullptr, ImOkStoredAs_LinearSrgb), linearCol, 3));
    const float reencodedCol[3]{ reencoded.r, reencoded.g, reencoded.b };
    CHECK(ReadsBack(Copy(CopyFormat::OtherFloats, linearCol, nullptr, ImOkStoredAs_LinearSrgb), reencodedCol, 3));

    for (int s{ 0 }; s < 2; ++s)
    {
        const ImOkStoredAs storage{ (s == 0) ? ImOkStoredAs_EncodedSrgb : ImOkStoredAs_LinearSrgb };
        const float* col{ (s == 0) ? CC4C33_ENCODED : linearCol };
        CAPTURE(s);
        CHECK(Copy(CopyFormat::Bytes, col, nullptr, storage) == "(204, 76, 51)");
        CHECK(Copy(CopyFormat::Hex, col, nullptr, storage) == "#CC4C33");
        CHECK(Copy(CopyFormat::OkLCh, col, nullptr, storage) == "oklch(58.60% 0.16761 33.00)");

        // HexAlpha needs alpha: no row, empty text
        char out[COPY_BUFFER_SIZE]{ 'x' };
        CHECK_FALSE(CopyText(CopyFormat::HexAlpha, col, nullptr, storage, out));
        CHECK(out[0] == '\0');
    }
}

TEST_CASE("Copy: 4-variants add alpha as-is, never encoded")
{
    const float alpha{ 0.5f };
    const float col4[4]{ CC4C33_ENCODED[0], CC4C33_ENCODED[1], CC4C33_ENCODED[2], alpha };
    const ImOkStoredAs storage{ ImOkStoredAs_EncodedSrgb };

    CHECK(Copy(CopyFormat::StoredFloats, col4, &alpha, storage) == "(0.8f, 0.29803923f, 0.2f, 0.5f)");
    const ImOk::LinearSrgb linear{ Cc4c33Linear() };
    const float linear4[4]{ linear.r, linear.g, linear.b, alpha };
    CHECK(ReadsBack(Copy(CopyFormat::OtherFloats, col4, &alpha, storage), linear4, 4)); // Alpha 0.5, not decoded
    CHECK(Copy(CopyFormat::Bytes, col4, &alpha, storage) == "(204, 76, 51, 128)");
    CHECK(Copy(CopyFormat::Hex, col4, &alpha, storage) == "#CC4C33");
    CHECK(Copy(CopyFormat::HexAlpha, col4, &alpha, storage) == "#CC4C3380");
    CHECK(Copy(CopyFormat::OkLCh, col4, &alpha, storage) == "oklch(58.60% 0.16761 33.00 / 0.500)");
}

TEST_CASE("Copy: stored floats are the data, unclamped")
{
    const float hdr[3]{ 4.0f, -0.25f, 0.4f };
    CHECK(Copy(CopyFormat::StoredFloats, hdr, nullptr, ImOkStoredAs_LinearSrgb) == "(4.0f, -0.25f, 0.4f)");
}

TEST_CASE("Copy: grays have hue none and chroma 0")
{
    for (int k{ 1 }; k <= 254; ++k)
    {
        const float gray[3]{ Byte(k), Byte(k), Byte(k) };
        const std::string text{ Copy(CopyFormat::OkLCh, gray, nullptr, ImOkStoredAs_EncodedSrgb) };
        CAPTURE(k);
        REQUIRE(text.find(" 0 none)") != std::string::npos);
    }
    const float gray128[3]{ Byte(128), Byte(128), Byte(128) };
    CHECK(Copy(CopyFormat::OkLCh, gray128, nullptr, ImOkStoredAs_EncodedSrgb) == "oklch(59.99% 0 none)");
    const float black[3]{};
    CHECK(Copy(CopyFormat::OkLCh, black, nullptr, ImOkStoredAs_EncodedSrgb) == "oklch(0.00% 0 none)");
}

TEST_CASE("Copy: oklch() brings every 8-bit color back byte-exact (every 5th value per channel, 0 to 255)")
{
    int wrong{ 0 };
    for (int r{ 0 }; r <= 255; r += 5)
    {
        for (int g{ 0 }; g <= 255; g += 5)
        {
            for (int b{ 0 }; b <= 255; b += 5)
            {
                const float col[3]{ Byte(r), Byte(g), Byte(b) };
                char out[COPY_BUFFER_SIZE]{};
                CopyText(CopyFormat::OkLCh, col, nullptr, ImOkStoredAs_EncodedSrgb, out);

                ImOk::OkLCh lch{};
                REQUIRE(ParseOkLCh(out, &lch));
                const ImOk::EncodedSrgb back{ ImOk::LinearSrgbToEncodedSrgb(ImOk::OkLabToLinearSrgb(ImOk::OkLChToOkLab(lch))) };
                using ImOk::Internal::ChannelToByte;
                if (ChannelToByte(back.r) != r || ChannelToByte(back.g) != g || ChannelToByte(back.b) != b)
                {
                    ++wrong;
                }
            }
        }
    }
    CHECK(wrong == 0);
}