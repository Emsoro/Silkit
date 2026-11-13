#include "core/profiles.h"

#include "core/logger.h"
#include "core/paths.h"

#include <Windows.h>

#include <fstream>
#include <system_error>

namespace silkit {

bool EnsureProfilesFile(const std::filesystem::path& file) {
    std::error_code ec;
    if (std::filesystem::exists(file, ec)) return true;

    const std::string seed = R"({"version":1,"connections":[]})";
    auto tmp = file;
    tmp += L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        out.write(seed.data(), static_cast<std::streamsize>(seed.size()));
        if (!out.good()) return false;
    }
    if (!MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    LogInfo("profiles: 创建 " + Paths::ToUtf8(file));
    return true;
}

}  // namespace silkit
