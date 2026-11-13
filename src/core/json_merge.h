#pragma once

#include <nlohmann/json.hpp>

namespace silkit {

/// 递归合并 JSON 对象：patch 中的对象与 base 深合并，其余类型（含数组）直接覆盖。
/// 用于 settings.set 的局部更新，避免前端每次提交完整配置。
nlohmann::json MergeJson(nlohmann::json base, const nlohmann::json& patch);

}  // namespace silkit
