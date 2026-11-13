#include "core/json_merge.h"

namespace silkit {

nlohmann::json MergeJson(nlohmann::json base, const nlohmann::json& patch) {
    if (!patch.is_object() || !base.is_object()) {
        return patch;
    }

    for (auto it = patch.begin(); it != patch.end(); ++it) {
        const auto existing = base.find(it.key());
        if (existing != base.end() && existing->is_object() && it.value().is_object()) {
            *existing = MergeJson(*existing, it.value());
        } else {
            base[it.key()] = it.value();
        }
    }
    return base;
}

}  // namespace silkit
