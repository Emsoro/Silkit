#pragma once

#include <string>

namespace silkit {

/// Windows DPAPI 加解密（用户绑定）。密文格式：`dpapi:v1:<base64>`。
bool ProtectSecret(const std::string& plain_utf8, std::string& out_stored, std::string& error);
bool UnprotectSecret(const std::string& stored, std::string& out_plain_utf8, std::string& error);

/// 从 connections.json 的 secret 对象读取密码；未保存/解密失败返回空。
std::string DecodeStoredSecret(const std::string& stored, std::string& error);

}  // namespace silkit
