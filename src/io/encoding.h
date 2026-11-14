#pragma once

#include <string>

namespace silkit::io {

/// M7-7：导入/导出支持的文本编码。UTF-8 默认不带 BOM。
enum class TextEncoding { Utf8, Utf8Bom, Utf16LE, Gbk };

const char* EncodingName(TextEncoding encoding);

/// 接受 "utf-8"/"utf8"/"utf-8-bom"/"utf-16le"/"gbk"/"auto" 等写法。
/// auto 返回 Utf8 并由调用方改用 DetectEncoding。
bool EncodingFromName(const std::string& name, TextEncoding& out);

bool EncodingHasBom(TextEncoding encoding);

/// BOM 优先；无 BOM 时按 UTF-8 合法性判断，非法回退 GBK。
TextEncoding DetectEncoding(const std::string& bytes);

/// 把文件字节解码为 UTF-8，并去掉 BOM。失败时 error 非空。
bool DecodeToUtf8(const std::string& bytes, TextEncoding encoding, std::string& out,
                  std::string& error);

/// UTF-8 → 目标编码；UTF-8/UTF-8BOM 会输出 BOM（若编码要求）。
bool EncodeFromUtf8(const std::string& utf8, TextEncoding encoding, std::string& out,
                    std::string& error);

}  // namespace silkit::io
