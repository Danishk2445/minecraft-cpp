// Language.h -- translated strings from assets/minecraft/lang/en_us.json.
#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

namespace mc {

class ResourcePack;

class Language {
public:
    bool load(const ResourcePack& pack, std::string_view code = "en_us");
    // The text for `key`, or the key itself when missing (like vanilla).
    const std::string& get(const std::string& key) const;
    bool has(const std::string& key) const { return strings_.contains(key); }
    size_t size() const { return strings_.size(); }

private:
    std::unordered_map<std::string, std::string> strings_;
    mutable std::string fallback_;
};

} // namespace mc
