#pragma once

#include <filesystem>

namespace silkit {

/// M0 只保证 profiles.json 存在且结构合法；连接管理在 M1 实现。
bool EnsureProfilesFile(const std::filesystem::path& file);

}  // namespace silkit
