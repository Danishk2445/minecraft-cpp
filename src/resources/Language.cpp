#include "resources/Language.h"

#include <format>

#include "core/Json.h"
#include "resources/ResourcePack.h"

namespace mc {

bool Language::load(const ResourcePack& pack, std::string_view code) {
    strings_.clear();
    auto j = loadJsonFile(pack.file("minecraft", std::format("lang/{}.json", code)));
    if (!j || !j->is_object()) return false;
    for (auto it = j->begin(); it != j->end(); ++it)
        if (it->is_string()) strings_.emplace(it.key(), it->get<std::string>());
    return true;
}

const std::string& Language::get(const std::string& key) const {
    auto it = strings_.find(key);
    if (it != strings_.end()) return it->second;
    fallback_ = key;
    return fallback_;
}

} // namespace mc
