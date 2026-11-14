#include "io/encoding.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace silkit::io {
namespace {

constexpr unsigned int kGbkCodePage = 936;

std::string Lower(const std::string& text) {
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool IsValidUtf8(const std::string& text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            ++i;
            continue;
        }
        int extra = 0;
        unsigned int code = 0;
        if ((c & 0xE0) == 0xC0) {
            extra = 1;
            code = c & 0x1Fu;
            if (code < 2) return false;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
            code = c & 0x0Fu;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3;
            code = c & 0x07u;
        } else {
            return false;
        }
        if (i + static_cast<std::size_t>(extra) >= text.size()) return false;
        for (int k = 1; k <= extra; ++k) {
            const unsigned char cont = static_cast<unsigned char>(text[i + static_cast<std::size_t>(k)]);
            if ((cont & 0xC0) != 0x80) return false;
            code = (code << 6) | (cont & 0x3Fu);
        }
        if (extra == 1 && code < 0x80) return false;
        if (extra == 2 && code < 0x800) return false;
        if (extra == 3 && code < 0x10000) return false;
        if (code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) return false;
        i += static_cast<std::size_t>(extra) + 1;
    }
    return true;
}

bool WideToUtf8(const std::wstring& wide, std::string& out) {
    if (wide.empty()) {
        out.clear();
        return true;
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return false;
    out.resize(static_cast<std::size_t>(needed));
    const int written = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                            out.data(), needed, nullptr, nullptr);
    if (written <= 0) return false;
    out.resize(static_cast<std::size_t>(written));
    return true;
}

bool MultiByteToWide(const std::string& bytes, unsigned int code_page, std::wstring& out) {
    if (bytes.empty()) {
        out.clear();
        return true;
    }
    const int needed = MultiByteToWideChar(code_page, 0, bytes.data(), static_cast<int>(bytes.size()),
                                           nullptr, 0);
    if (needed <= 0) return false;
    out.resize(static_cast<std::size_t>(needed));
    const int written = MultiByteToWideChar(code_page, 0, bytes.data(), static_cast<int>(bytes.size()),
                                            out.data(), needed);
    if (written <= 0) return false;
    out.resize(static_cast<std::size_t>(written));
    return true;
}

bool WideToMultiByte(const std::wstring& wide, unsigned int code_page, std::string& out) {
    if (wide.empty()) {
        out.clear();
        return true;
    }
    const int needed = WideCharToMultiByte(code_page, 0, wide.data(), static_cast<int>(wide.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return false;
    out.resize(static_cast<std::size_t>(needed));
    const int written = WideCharToMultiByte(code_page, 0, wide.data(), static_cast<int>(wide.size()),
                                            out.data(), needed, nullptr, nullptr);
    if (written <= 0) return false;
    out.resize(static_cast<std::size_t>(written));
    return true;
}

bool DecodeUtf16Le(const std::string& bytes, std::size_t offset, std::string& out,
                   std::string& error) {
    const std::size_t size = bytes.size() - offset;
    if (size % 2 != 0) {
        error = "UTF-16LE 文件字节数为奇数，无法解码";
        return false;
    }
    std::wstring wide;
    wide.resize(size / 2);
    for (std::size_t i = 0; i < wide.size(); ++i) {
        const unsigned char low = static_cast<unsigned char>(bytes[offset + i * 2]);
        const unsigned char high = static_cast<unsigned char>(bytes[offset + i * 2 + 1]);
        wide[i] = static_cast<wchar_t>(low | (static_cast<unsigned int>(high) << 8));
    }
    if (!WideToUtf8(wide, out)) {
        error = "UTF-16LE 解码失败";
        return false;
    }
    return true;
}

}  // namespace

const char* EncodingName(TextEncoding encoding) {
    switch (encoding) {
        case TextEncoding::Utf8Bom: return "utf-8-bom";
        case TextEncoding::Utf16LE: return "utf-16le";
        case TextEncoding::Gbk: return "gbk";
        case TextEncoding::Utf8:
        default: return "utf-8";
    }
}

bool EncodingFromName(const std::string& name, TextEncoding& out) {
    const std::string key = Lower(name);
    if (key.empty() || key == "utf-8" || key == "utf8") {
        out = TextEncoding::Utf8;
        return true;
    }
    if (key == "utf-8-bom" || key == "utf8bom" || key == "utf-8 bom") {
        out = TextEncoding::Utf8Bom;
        return true;
    }
    if (key == "utf-16le" || key == "utf16le" || key == "utf16" || key == "utf-16") {
        out = TextEncoding::Utf16LE;
        return true;
    }
    if (key == "gbk" || key == "gb18030" || key == "cp936") {
        out = TextEncoding::Gbk;
        return true;
    }
    return false;
}

bool EncodingHasBom(TextEncoding encoding) {
    return encoding == TextEncoding::Utf8Bom || encoding == TextEncoding::Utf16LE;
}

TextEncoding DetectEncoding(const std::string& bytes) {
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        return TextEncoding::Utf8Bom;
    }
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF &&
        static_cast<unsigned char>(bytes[1]) == 0xFE) {
        return TextEncoding::Utf16LE;
    }
    if (IsValidUtf8(bytes)) return TextEncoding::Utf8;
    return TextEncoding::Gbk;
}

bool DecodeToUtf8(const std::string& bytes, TextEncoding encoding, std::string& out,
                  std::string& error) {
    error.clear();
    out.clear();
    std::size_t offset = 0;
    switch (encoding) {
        case TextEncoding::Utf8:
        case TextEncoding::Utf8Bom:
            if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
                static_cast<unsigned char>(bytes[1]) == 0xBB &&
                static_cast<unsigned char>(bytes[2]) == 0xBF) {
                offset = 3;
            }
            out.assign(bytes, offset, std::string::npos);
            if (!IsValidUtf8(out)) {
                error = "文件不是合法的 UTF-8 文本，请选择 GBK 编码";
                return false;
            }
            return true;
        case TextEncoding::Utf16LE: {
            if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF &&
                static_cast<unsigned char>(bytes[1]) == 0xFE) {
                offset = 2;
            }
            return DecodeUtf16Le(bytes, offset, out, error);
        }
        case TextEncoding::Gbk: {
            std::wstring wide;
            if (!MultiByteToWide(bytes, kGbkCodePage, wide)) {
                error = "GBK 解码失败";
                return false;
            }
            if (!WideToUtf8(wide, out)) {
                error = "GBK 转 UTF-8 失败";
                return false;
            }
            return true;
        }
    }
    error = "未知编码";
    return false;
}

bool EncodeFromUtf8(const std::string& utf8, TextEncoding encoding, std::string& out,
                    std::string& error) {
    error.clear();
    out.clear();
    switch (encoding) {
        case TextEncoding::Utf8:
            out = utf8;
            return true;
        case TextEncoding::Utf8Bom:
            out.reserve(utf8.size() + 3);
            out.append("\xEF\xBB\xBF", 3);
            out.append(utf8);
            return true;
        case TextEncoding::Utf16LE: {
            std::wstring wide;
            if (!MultiByteToWide(utf8, CP_UTF8, wide)) {
                error = "UTF-8 转 UTF-16 失败";
                return false;
            }
            out.reserve(wide.size() * 2 + 2);
            out.push_back(static_cast<char>(0xFF));
            out.push_back(static_cast<char>(0xFE));
            for (wchar_t unit : wide) {
                out.push_back(static_cast<char>(static_cast<unsigned int>(unit) & 0xFFu));
                out.push_back(static_cast<char>((static_cast<unsigned int>(unit) >> 8) & 0xFFu));
            }
            return true;
        }
        case TextEncoding::Gbk: {
            std::wstring wide;
            if (!MultiByteToWide(utf8, CP_UTF8, wide) || !WideToMultiByte(wide, kGbkCodePage, out)) {
                error = "UTF-8 转 GBK 失败（可能包含 GBK 不支持的字符）";
                return false;
            }
            return true;
        }
    }
    error = "未知编码";
    return false;
}

}  // namespace silkit::io
