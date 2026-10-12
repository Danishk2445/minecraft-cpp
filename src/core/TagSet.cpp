#include "core/TagSet.h"

#include <algorithm>
#include <bit>
#include <format>

#include "core/Json.h"

namespace mc {
namespace {

std::string_view stripNamespace(std::string_view id) {
    if (id.starts_with('#')) id.remove_prefix(1);
    if (id.starts_with("minecraft:")) id.remove_prefix(10);
    return id;
}

struct RawTag {
    std::vector<std::string> values, exclude;
};

} // namespace

bool globMatch(std::string_view pattern, std::string_view text) {
    // Iterative matcher with backtracking to the last '*'.
    size_t p = 0, t = 0, star = std::string_view::npos, mark = 0;
    while (t < text.size()) {
        if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            mark = t;
        } else if (p < pattern.size() && pattern[p] == text[t]) {
            ++p;
            ++t;
        } else if (star != std::string_view::npos) {
            p = star + 1;
            t = ++mark;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

bool TagSet::load(const std::filesystem::path& dir, size_t count, const std::function<std::string_view(size_t)>& idOf,
                  std::string_view what, std::string* error) {
    names_.clear();
    index_.clear();
    bits_.clear();
    std::vector<std::string> problems;
    std::vector<RawTag> raw;

    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        if (error) *error = std::format("no tag directory {}", dir.string());
        return false;
    }
    std::vector<std::filesystem::path> files;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, ec); !ec && it != std::filesystem::end(it);
         it.increment(ec))
        if (it->is_regular_file() && it->path().extension() == ".json") files.push_back(it->path());
    std::sort(files.begin(), files.end()); // stable TagIds
    for (const auto& file : files) {
        std::string rel = std::filesystem::relative(file, dir).replace_extension().generic_string();
        std::string err;
        auto doc = loadJsonFile(file, &err);
        if (!doc || !doc->is_object()) {
            problems.push_back(err.empty() ? std::format("{}: not a JSON object", file.string()) : err);
            continue;
        }
        RawTag t;
        auto readList = [&](const char* key, std::vector<std::string>& out) {
            if (auto it = doc->find(key); it != doc->end() && it->is_array())
                for (const auto& v : *it)
                    if (v.is_string()) out.push_back(v.get<std::string>());
        };
        readList("values", t.values);
        readList("exclude", t.exclude);
        index_.emplace("minecraft:" + rel, TagId(names_.size()));
        names_.push_back("minecraft:" + rel);
        raw.push_back(std::move(t));
    }

    std::unordered_map<std::string_view, size_t> byId;
    for (size_t i = 0; i < count; ++i) byId.emplace(stripNamespace(idOf(i)), i);
    const size_t words = (count + 63) / 64;
    bits_.assign(names_.size(), std::vector<uint64_t>(words, 0));
    // 0 = unresolved, 1 = resolving (cycle guard), 2 = done.
    std::vector<uint8_t> state(names_.size(), 0);
    std::function<void(TagId)> resolve = [&](TagId t) {
        if (state[t] == 2) return;
        if (state[t] == 1) {
            problems.push_back(std::format("tag {} references itself", names_[t]));
            return;
        }
        state[t] = 1;
        auto apply = [&](const std::string& entry, bool add, std::vector<uint64_t>& bits) {
            auto set = [&](size_t b) {
                if (add) bits[b >> 6] |= 1ull << (b & 63);
                else bits[b >> 6] &= ~(1ull << (b & 63));
            };
            const std::string_view name = stripNamespace(entry);
            if (entry.starts_with('#')) {
                auto it = index_.find("minecraft:" + std::string(name));
                if (it == index_.end()) {
                    problems.push_back(std::format("tag {}: unknown tag {}", names_[t], entry));
                    return;
                }
                resolve(it->second);
                const auto& other = bits_[it->second];
                for (size_t w = 0; w < words; ++w)
                    for (uint64_t m = other[w]; m; m &= m - 1) set(w * 64 + size_t(std::countr_zero(m)));
                return;
            }
            if (name.find('*') != std::string_view::npos) {
                bool any = false;
                for (size_t i = 0; i < count; ++i)
                    if (globMatch(name, stripNamespace(idOf(i)))) set(i), any = true;
                if (!any) problems.push_back(std::format("tag {}: {} matches no {}", names_[t], entry, what));
                return;
            }
            auto it = byId.find(name);
            if (it == byId.end()) {
                problems.push_back(std::format("tag {}: unknown {} {}", names_[t], what, entry));
                return;
            }
            set(it->second);
        };
        std::vector<uint64_t> bits(words, 0);
        for (const auto& v : raw[t].values) apply(v, true, bits);
        for (const auto& v : raw[t].exclude) apply(v, false, bits);
        bits_[t] = std::move(bits);
        state[t] = 2;
    };
    for (TagId t = 0; t < names_.size(); ++t) resolve(t);

    if (!problems.empty() && error) {
        std::string msg;
        for (const auto& p : problems) msg += p + "\n";
        *error = msg;
    }
    return problems.empty();
}

std::optional<TagId> TagSet::find(std::string_view name) const {
    auto it = index_.find("minecraft:" + std::string(stripNamespace(name)));
    if (it == index_.end()) return std::nullopt;
    return it->second;
}

std::vector<uint16_t> TagSet::members(TagId t) const {
    std::vector<uint16_t> out;
    for (size_t w = 0; w < bits_[t].size(); ++w)
        for (uint64_t m = bits_[t][w]; m; m &= m - 1) out.push_back(uint16_t(w * 64 + size_t(std::countr_zero(m))));
    return out;
}

} // namespace mc
