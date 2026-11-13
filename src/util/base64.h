#pragma once

#include <cstddef>
#include <string>

namespace silkit::util {

/// 标准 Base64（RFC 4648，含 `=` 填充）。用于二进制值的 IPC 传输。
std::string Base64Encode(const void* data, std::size_t size);

/// 解码 Base64；允许空白字符，非法输入返回 false 且不修改 out。
bool Base64Decode(const std::string& text, std::string& out);

}  // namespace silkit::util
