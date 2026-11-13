#pragma once

// 版本号单一来源：CMake 通过 target_compile_definitions 注入。
// 直接编译（无 CMake）时使用下面的兜底值。
#ifndef SILKIT_VERSION
#define SILKIT_VERSION "0.1.0"
#endif

namespace silkit {

inline constexpr const char* kAppName = "Silkit";
inline constexpr const char* kVersion = SILKIT_VERSION;
inline constexpr const char* kDataDirName = "Silkit";

}  // namespace silkit
