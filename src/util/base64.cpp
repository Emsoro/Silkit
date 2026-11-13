#include "util/base64.h"

#include <array>
#include <cstdint>
#include <utility>

namespace silkit::util {
namespace {

constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int DecodeChar(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return static_cast<int>(c - 'A');
    if (c >= 'a' && c <= 'z') return static_cast<int>(c - 'a') + 26;
    if (c >= '0' && c <= '9') return static_cast<int>(c - '0') + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

bool IsSpace(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

}  // namespace

std::string Base64Encode(const void* data, std::size_t size) {
    if (data == nullptr || size == 0) return {};

    const auto* bytes = static_cast<const unsigned char*>(data);
    std::string out;
    out.reserve(((size + 2) / 3) * 4);

    std::size_t i = 0;
    while (i + 2 < size) {
        const std::uint32_t chunk = (static_cast<std::uint32_t>(bytes[i]) << 16) |
                                    (static_cast<std::uint32_t>(bytes[i + 1]) << 8) |
                                    static_cast<std::uint32_t>(bytes[i + 2]);
        out.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 6) & 0x3F]);
        out.push_back(kAlphabet[chunk & 0x3F]);
        i += 3;
    }

    const std::size_t remaining = size - i;
    if (remaining == 1) {
        const std::uint32_t chunk = static_cast<std::uint32_t>(bytes[i]) << 16;
        out.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (remaining == 2) {
        const std::uint32_t chunk = (static_cast<std::uint32_t>(bytes[i]) << 16) |
                                    (static_cast<std::uint32_t>(bytes[i + 1]) << 8);
        out.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

bool Base64Decode(const std::string& text, std::string& out) {
    std::string compact;
    compact.reserve(text.size());
    for (char ch : text) {
        const auto byte = static_cast<unsigned char>(ch);
        if (IsSpace(byte)) continue;
        compact.push_back(ch);
    }

    if (compact.empty()) {
        out.clear();
        return true;
    }
    if (compact.size() % 4 != 0) return false;

    std::string decoded;
    decoded.reserve((compact.size() / 4) * 3);

    for (std::size_t i = 0; i < compact.size(); i += 4) {
        std::array<int, 4> values{{0, 0, 0, 0}};
        int padding = 0;
        for (int j = 0; j < 4; ++j) {
            const char ch = compact[i + static_cast<std::size_t>(j)];
            if (ch == '=') {
                // 只允许末尾连续 1~2 个 '='
                if (i + 4 != compact.size() || j < 2) return false;
                values[static_cast<std::size_t>(j)] = 0;
                ++padding;
            } else {
                if (padding > 0) return false;
                const int value = DecodeChar(static_cast<unsigned char>(ch));
                if (value < 0) return false;
                values[static_cast<std::size_t>(j)] = value;
            }
        }

        const std::uint32_t chunk = (static_cast<std::uint32_t>(values[0]) << 18) |
                                    (static_cast<std::uint32_t>(values[1]) << 12) |
                                    (static_cast<std::uint32_t>(values[2]) << 6) |
                                    static_cast<std::uint32_t>(values[3]);
        decoded.push_back(static_cast<char>((chunk >> 16) & 0xFF));
        if (padding < 2) decoded.push_back(static_cast<char>((chunk >> 8) & 0xFF));
        if (padding < 1) decoded.push_back(static_cast<char>(chunk & 0xFF));
    }

    out = std::move(decoded);
    return true;
}

}  // namespace silkit::util
