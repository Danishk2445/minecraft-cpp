#include "game/CreativeTabs.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <unordered_set>

#include "core/Json.h"
#include "game/Item.h"
#include "resources/ResourcePack.h"

namespace mc {
namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

} // namespace

bool CreativeTabs::load(const std::filesystem::path& dataDir, const ItemRegistry& items, std::string* error) {
    tabs_.clear();
    searchable_.clear();
    std::string err;
    auto doc = loadJsonFile(dataDir / "creative_tabs.json", &err);
    if (!doc || !doc->is_object()) {
        if (error) *error = err.empty() ? "creative_tabs.json: expected an object" : err;
        return false;
    }
    const Json families = doc->value("families", Json::object());
    std::vector<std::string> problems;
    for (const Json& t : doc->value("tabs", Json::array())) {
        CreativeTab tab;
        tab.id = t.value("id", std::string());
        tab.nameKey = t.value("name", std::string());
        tab.icon = items.find(t.value("icon", std::string("stone")));
        tab.top = t.value("row", std::string("top")) == "top";
        tab.column = t.value("column", 0);
        tab.hidden = t.value("hidden", false);
        std::unordered_set<const Item*> seen;
        auto add = [&](const Item* item) {
            if (item && seen.insert(item).second) tab.items.push_back(item);
        };
        for (const Json& e : t.value("items", Json::array())) {
            if (e.is_object()) {
                // {"each": family, "items": templates}: every template per member, in order.
                const std::string family = e.value("each", std::string());
                if (!families.contains(family)) problems.push_back(std::format("{}: unknown family {}", tab.id, family));
                for (const Json& member : families.value(family, Json::array()))
                    for (const Json& tpl : e.value("items", Json::array())) {
                        std::string id = tpl.get<std::string>();
                        if (const size_t at = id.find("{}"); at != std::string::npos)
                            id.replace(at, 2, member.get<std::string>());
                        add(items.find(id)); // members without that item are skipped
                    }
            } else if (const std::string id = e.get<std::string>(); id.find('*') != std::string::npos) {
                std::vector<const Item*> hits;
                for (size_t i = 0; i < items.size(); ++i)
                    if (globMatch(id, shortId(items.get(i).id))) hits.push_back(&items.get(i));
                std::sort(hits.begin(), hits.end(), [](const Item* a, const Item* b) { return a->id < b->id; });
                for (const Item* h : hits) add(h);
            } else if (const Item* item = items.find(id)) {
                add(item);
            } else {
                problems.push_back(std::format("{}: unknown item {}", tab.id, id));
            }
        }
        tabs_.push_back(std::move(tab));
    }
    std::unordered_set<const Item*> seen;
    for (const CreativeTab& tab : tabs_)
        if (!tab.hidden)
            for (const Item* item : tab.items)
                if (seen.insert(item).second) searchable_.push_back(item);
    if (error) {
        error->clear();
        for (const auto& p : problems) *error += p + "\n";
    }
    return problems.empty();
}

std::vector<const Item*> CreativeTabs::search(std::string_view query,
                                              const std::function<std::string(const Item&)>& displayName) const {
    const std::string q = lower(query);
    std::vector<const Item*> out;
    for (const Item* item : searchable_) {
        if (q.empty() || lower(displayName(*item)).find(q) != std::string::npos ||
            lower(shortId(item->id)).find(q) != std::string::npos)
            out.push_back(item);
    }
    return out;
}

} // namespace mc
