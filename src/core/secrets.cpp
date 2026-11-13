#include "core/secrets.h"

#include "util/base64.h"

#include <Windows.h>
#include <dpapi.h>

#include <cstring>

namespace silkit {
namespace {

constexpr char kPrefix[] = "dpapi:v1:";

DATA_BLOB EntropyBlob() {
    static const char kEntropy[] = "Silkit.Connection.v1";
    DATA_BLOB blob{};
    blob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(kEntropy));
    blob.cbData = static_cast<DWORD>(sizeof(kEntropy) - 1);
    return blob;
}

}  // namespace

bool ProtectSecret(const std::string& plain_utf8, std::string& out_stored, std::string& error) {
    if (plain_utf8.empty()) {
        out_stored.clear();
        return true;
    }

    DATA_BLOB input{};
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plain_utf8.data()));
    input.cbData = static_cast<DWORD>(plain_utf8.size());
    DATA_BLOB entropy = EntropyBlob();
    DATA_BLOB output{};

    if (!CryptProtectData(&input, L"Silkit", &entropy, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        error = "DPAPI 加密失败，错误码 " + std::to_string(GetLastError());
        return false;
    }

    const std::string base64 = util::Base64Encode(output.pbData, output.cbData);
    if (output.pbData != nullptr) {
        SecureZeroMemory(output.pbData, output.cbData);
        LocalFree(output.pbData);
    }

    out_stored = std::string(kPrefix) + base64;
    return true;
}

bool UnprotectSecret(const std::string& stored, std::string& out_plain_utf8, std::string& error) {
    if (stored.empty()) {
        out_plain_utf8.clear();
        return true;
    }
    if (stored.compare(0, sizeof(kPrefix) - 1, kPrefix) != 0) {
        error = "未知的密码存储格式";
        return false;
    }

    std::string bytes;
    if (!util::Base64Decode(stored.substr(sizeof(kPrefix) - 1), bytes)) {
        error = "密码密文损坏";
        return false;
    }
    if (bytes.empty()) {
        out_plain_utf8.clear();
        return true;
    }

    DATA_BLOB input{};
    input.pbData = reinterpret_cast<BYTE*>(bytes.data());
    input.cbData = static_cast<DWORD>(bytes.size());
    DATA_BLOB entropy = EntropyBlob();
    DATA_BLOB output{};

    if (!CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        error = "DPAPI 解密失败，错误码 " + std::to_string(GetLastError());
        return false;
    }

    out_plain_utf8.assign(reinterpret_cast<char*>(output.pbData), output.cbData);
    if (output.pbData != nullptr) {
        SecureZeroMemory(output.pbData, output.cbData);
        LocalFree(output.pbData);
    }
    return true;
}

std::string DecodeStoredSecret(const std::string& stored, std::string& error) {
    std::string plain;
    if (!UnprotectSecret(stored, plain, error)) {
        plain.clear();
    }
    return plain;
}

}  // namespace silkit
