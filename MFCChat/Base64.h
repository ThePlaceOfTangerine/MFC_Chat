#pragma once

// Base64 đơn giản, chỉ gồm header, dùng chung cho server và client.
// Dùng để đưa dữ liệu nhị phân (ảnh, video, avatar) vào các gói JSON.

#include <cstddef>
#include <cstdint>
#include <string>

namespace b64
{
    inline std::string Encode(const unsigned char* data, size_t length)
    {
        static const char table[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::string out;
        out.reserve(((length + 2) / 3) * 4);

        size_t i = 0;

        while (i + 2 < length)
        {
            const uint32_t value =
                (static_cast<uint32_t>(data[i]) << 16) |
                (static_cast<uint32_t>(data[i + 1]) << 8) |
                static_cast<uint32_t>(data[i + 2]);

            out.push_back(table[(value >> 18) & 0x3F]);
            out.push_back(table[(value >> 12) & 0x3F]);
            out.push_back(table[(value >> 6) & 0x3F]);
            out.push_back(table[value & 0x3F]);

            i += 3;
        }

        if (i + 1 == length)
        {
            const uint32_t value = static_cast<uint32_t>(data[i]) << 16;

            out.push_back(table[(value >> 18) & 0x3F]);
            out.push_back(table[(value >> 12) & 0x3F]);
            out.push_back('=');
            out.push_back('=');
        }
        else if (i + 2 == length)
        {
            const uint32_t value =
                (static_cast<uint32_t>(data[i]) << 16) |
                (static_cast<uint32_t>(data[i + 1]) << 8);

            out.push_back(table[(value >> 18) & 0x3F]);
            out.push_back(table[(value >> 12) & 0x3F]);
            out.push_back(table[(value >> 6) & 0x3F]);
            out.push_back('=');
        }

        return out;
    }

    inline std::string Encode(const std::string& data)
    {
        return Encode(
            reinterpret_cast<const unsigned char*>(data.data()),
            data.size());
    }

    // Trả về false nếu chuỗi không phải Base64 hợp lệ.
    inline bool Decode(const std::string& input, std::string& output)
    {
        output.clear();

        if (input.size() % 4 != 0)
            return false;

        auto valueOf = [](char c) -> int
        {
            if (c >= 'A' && c <= 'Z') return c - 'A';
            if (c >= 'a' && c <= 'z') return c - 'a' + 26;
            if (c >= '0' && c <= '9') return c - '0' + 52;
            if (c == '+') return 62;
            if (c == '/') return 63;
            return -1;
        };

        output.reserve((input.size() / 4) * 3);

        for (size_t i = 0; i < input.size(); i += 4)
        {
            const char c0 = input[i];
            const char c1 = input[i + 1];
            const char c2 = input[i + 2];
            const char c3 = input[i + 3];

            const int v0 = valueOf(c0);
            const int v1 = valueOf(c1);

            if (v0 < 0 || v1 < 0)
                return false;

            const bool pad2 = (c2 == '=');
            const bool pad3 = (c3 == '=');

            // Chỉ cho phép dấu '=' ở khối cuối.
            if ((pad2 || pad3) && i + 4 != input.size())
                return false;

            if (pad2 && !pad3)
                return false;

            const int v2 = pad2 ? 0 : valueOf(c2);
            const int v3 = pad3 ? 0 : valueOf(c3);

            if ((!pad2 && v2 < 0) || (!pad3 && v3 < 0))
                return false;

            const uint32_t value =
                (static_cast<uint32_t>(v0) << 18) |
                (static_cast<uint32_t>(v1) << 12) |
                (static_cast<uint32_t>(v2) << 6) |
                static_cast<uint32_t>(v3);

            output.push_back(static_cast<char>((value >> 16) & 0xFF));

            if (!pad2)
                output.push_back(static_cast<char>((value >> 8) & 0xFF));

            if (!pad3)
                output.push_back(static_cast<char>(value & 0xFF));
        }

        return true;
    }
}