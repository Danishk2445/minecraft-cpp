#include "game/Recipes.h"

#include <algorithm>
#include <format>
#include <functional>
#include <optional>
#include <set>

#include "core/Json.h"
#include "resources/ResourcePack.h"

namespace mc {
namespace {

std::optional<RecipeType> recipeType(std::string_view t) {
    if (t.starts_with("minecraft:")) t.remove_prefix(10);
    if (t == "shaped" || t == "crafting_shaped") return RecipeType::Shaped;
    if (t == "shapeless" || t == "crafting_shapeless") return RecipeType::Shapeless;
    if (t == "smelting") return RecipeType::Smelting;
    if (t == "blasting") return RecipeType::Blasting;
    if (t == "smoking") return RecipeType::Smoking;
    if (t == "campfire_cooking") return RecipeType::CampfireCooking;
    return std::nullopt;
}

int cookingIndex(RecipeType t) { return int(t) - int(RecipeType::Smelting); }

// "{var}" placeholders in every string of a template, from one row of a family. False when
// a variable is missing (the template doesn't apply to that row).
bool substitute(Json& j, const Json& row) {
    if (j.is_string()) {
        std::string s = j.get<std::string>(), out;
        for (size_t i = 0; i < s.size();) {
            const size_t open = s.find('{', i);
            if (open == std::string::npos) {
                out += s.substr(i);
                break;
            }
            const size_t close = s.find('}', open);
            if (close == std::string::npos) {
                out += s.substr(i);
                break;
            }
            out += s.substr(i, open - i);
            const std::string var = s.substr(open + 1, close - open - 1);
            auto v = row.find(var);
            if (v == row.end() || !v->is_string()) return false;
            out += v->get<std::string>();
            i = close + 1;
        }
        j = out;
        return true;
    }
    if (j.is_array()) {
        for (Json& e : j)
            if (!substitute(e, row)) return false;
        return true;
    }
    if (j.is_object()) {
        for (auto& [k, v] : j.items())
            if (!substitute(v, row)) return false;
    }
    return true;
}

// The filled part of a grid (vanilla CraftingInput.of): its bounds and size.
struct Trimmed {
    int left = 0, top = 0, width = 0, height = 0, count = 0;
};

Trimmed trim(std::span<const ItemStack> grid, int width, int height) {
    int x0 = width, y0 = height, x1 = -1, y1 = -1, n = 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (!grid[size_t(y * width + x)].empty()) {
                x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
                ++n;
            }
    if (n == 0) return {};
    return {x0, y0, x1 - x0 + 1, y1 - y0 + 1, n};
}

bool matchesShaped(const Recipe& r, std::span<const ItemStack> grid, int gridWidth, const Trimmed& t, bool mirrored) {
    for (int y = 0; y < r.height; ++y)
        for (int x = 0; x < r.width; ++x) {
            const Ingredient& ing = r.ingredients[size_t(y * r.width + (mirrored ? r.width - 1 - x : x))];
            const ItemStack& s = grid[size_t((t.top + y) * gridWidth + t.left + x)];
            if (ing.empty() ? !s.empty() : !ing.test(s)) return false;
        }
    return true;
}

bool matchesShapeless(const Recipe& r, std::span<const ItemStack> grid) {
    std::vector<const ItemStack*> items;
    for (const ItemStack& s : grid)
        if (!s.empty()) items.push_back(&s);
    if (items.size() != r.ingredients.size()) return false;
    // Pair ingredients with items by backtracking (at most 9 of each).
    std::vector<bool> used(items.size(), false);
    std::function<bool(size_t)> assign = [&](size_t i) {
        if (i == r.ingredients.size()) return true;
        for (size_t k = 0; k < items.size(); ++k)
            if (!used[k] && r.ingredients[i].test(*items[k])) {
                used[k] = true;
                if (assign(i + 1)) return true;
                used[k] = false;
            }
        return false;
    };
    return assign(0);
}

} // namespace

bool Ingredient::test(const ItemStack& s) const {
    return !s.empty() && std::find(items.begin(), items.end(), s.item) != items.end();
}

bool RecipeBook::load(const std::filesystem::path& dataDir, const ItemRegistry& items, std::string* error) {
    items_ = &items;
    recipes_.clear();
    std::vector<std::string> problems;
    const std::filesystem::path dir = dataDir / "recipes";

    auto ingredient = [&](const Json& j, Ingredient& out, const std::string& where) {
        auto addOne = [&](const std::string& entry) {
            if (entry.starts_with('#')) {
                const auto tag = items.tags().find(entry);
                if (!tag) {
                    problems.push_back(std::format("{}: unknown item tag {}", where, entry));
                    return false;
                }
                for (uint16_t i : items.tags().members(*tag)) out.items.push_back(&items.get(i));
                return true;
            }
            const Item* item = items.find(entry);
            if (!item) {
                problems.push_back(std::format("{}: unknown item {}", where, entry));
                return false;
            }
            out.items.push_back(item);
            return true;
        };
        bool ok = true;
        if (j.is_string()) ok = addOne(j.get<std::string>());
        else if (j.is_array())
            for (const Json& e : j) ok = e.is_string() && addOne(e.get<std::string>()) && ok;
        else ok = false;
        std::sort(out.items.begin(), out.items.end());
        out.items.erase(std::unique(out.items.begin(), out.items.end()), out.items.end());
        return ok && !out.items.empty();
    };

    auto addRecipe = [&](const Json& j, const std::string& where) {
        const auto type = recipeType(j.value("type", std::string()));
        if (!type) {
            problems.push_back(std::format("{}: unknown recipe type {}", where, j.value("type", std::string())));
            return;
        }
        Recipe r;
        r.type = *type;
        r.source = where;
        const Json result = j.value("result", Json());
        const std::string resultId = result.is_string() ? result.get<std::string>() : result.value("id", std::string());
        const Item* out = items.find(resultId);
        if (!out) {
            problems.push_back(std::format("{}: unknown result {}", where, resultId));
            return;
        }
        r.result = {out, result.is_object() ? std::clamp(result.value("count", 1), 1, out->maxStack) : 1};
        bool ok = true;
        switch (r.type) {
        case RecipeType::Shaped: {
            const Json pattern = j.value("pattern", Json::array()), key = j.value("key", Json::object());
            r.height = int(pattern.size());
            r.width = r.height > 0 && pattern[0].is_string() ? int(pattern[0].get<std::string>().size()) : 0;
            if (r.height < 1 || r.height > 3 || r.width < 1 || r.width > 3) {
                problems.push_back(where + ": the pattern must be 1 to 3 rows of 1 to 3 cells");
                return;
            }
            for (const Json& row : pattern) {
                const std::string line = row.is_string() ? row.get<std::string>() : std::string();
                if (int(line.size()) != r.width) {
                    problems.push_back(where + ": pattern rows differ in length");
                    return;
                }
                for (char c : line) {
                    Ingredient ing;
                    if (c != ' ') {
                        auto it = key.find(std::string(1, c));
                        if (it == key.end()) {
                            problems.push_back(std::format("{}: key '{}' is not defined", where, c));
                            return;
                        }
                        ok = ingredient(*it, ing, where) && ok;
                    }
                    r.ingredients.push_back(std::move(ing));
                }
            }
            break;
        }
        case RecipeType::Shapeless: {
            const Json list = j.value("ingredients", Json::array());
            if (list.empty() || list.size() > 9) {
                problems.push_back(where + ": 1 to 9 ingredients expected");
                return;
            }
            for (const Json& e : list) {
                Ingredient ing;
                ok = ingredient(e, ing, where) && ok;
                r.ingredients.push_back(std::move(ing));
            }
            break;
        }
        default: {
            Ingredient ing;
            ok = ingredient(j.value("ingredient", Json()), ing, where);
            r.ingredients.push_back(std::move(ing));
            r.experience = j.value("experience", 0.0f);
            const int defaultTime = r.type == RecipeType::Smelting ? 200 : r.type == RecipeType::CampfireCooking ? 600 : 100;
            r.cookingTime = std::max(1, j.value("cookingtime", defaultTime));
            break;
        }
        }
        if (ok) recipes_.push_back(std::move(r));
    };

    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, ec); !ec && it != std::filesystem::end(it);
         it.increment(ec))
        if (it->is_regular_file() && it->path().extension() == ".json") files.push_back(it->path());
    std::sort(files.begin(), files.end());
    if (files.empty()) problems.push_back("no recipe files in " + dir.string());
    for (const auto& file : files) {
        const std::string name = std::filesystem::relative(file, dir).generic_string();
        std::string err;
        auto doc = loadJsonFile(file, &err);
        if (!doc || !doc->is_object()) {
            problems.push_back(err.empty() ? name + ": not a JSON object" : err);
            continue;
        }
        if (doc->contains("families")) {
            const Json families = doc->value("families", Json::array());
            for (const Json& family : families) {
                const std::string fname = family.value("name", std::string("?"));
                const Json rows = family.value("rows", Json::array()), templates = family.value("recipes", Json::array());
                for (const Json& row : rows)
                    for (size_t t = 0; t < templates.size(); ++t) {
                        Json recipe = templates[t];
                        if (!substitute(recipe, row)) continue; // the row lacks a variable: not for it
                        addRecipe(recipe, std::format("{} {} #{}", name, fname, t));
                    }
            }
        }
        const Json list = doc->value("recipes", Json::array());
        for (size_t i = 0; i < list.size(); ++i) addRecipe(list[i], std::format("{} #{}", name, i));
    }

    for (auto& byItem : cookingByItem_) byItem.assign(items.size(), {});
    for (const Recipe& r : recipes_)
        if (!r.crafting())
            for (const Item* item : r.ingredients[0].items) cookingByItem_[cookingIndex(r.type)][item->index].push_back(&r);

    if (error) {
        error->clear();
        for (const auto& p : problems) *error += p + "\n";
    }
    return problems.empty();
}

const Recipe* RecipeBook::findCrafting(std::span<const ItemStack> grid, int width, int height) const {
    const Trimmed t = trim(grid, width, height);
    if (t.count == 0) return nullptr;
    for (const Recipe& r : recipes_) {
        if (r.type == RecipeType::Shaped) {
            if (r.width != t.width || r.height != t.height) continue;
            if (matchesShaped(r, grid, width, t, false) || matchesShaped(r, grid, width, t, true)) return &r;
        } else if (r.type == RecipeType::Shapeless) {
            if (int(r.ingredients.size()) == t.count && matchesShapeless(r, grid)) return &r;
        }
    }
    return nullptr;
}

RecipeBook::Crafted RecipeBook::craft(std::span<const ItemStack> grid, int width, int height) const {
    if (const Recipe* r = findCrafting(grid, width, height)) return {r->result, r};
    // Vanilla RepairItemRecipe: two damaged copies of one tool become one, with their
    // remaining durability added plus a 5% bonus.
    std::vector<const ItemStack*> filled;
    for (const ItemStack& s : grid)
        if (!s.empty()) filled.push_back(&s);
    if (filled.size() == 2 && filled[0]->item == filled[1]->item && filled[0]->damageable() && filled[0]->count == 1 &&
        filled[1]->count == 1) {
        const int max = filled[0]->item->maxDamage;
        const int remaining = (max - filled[0]->damage) + (max - filled[1]->damage) + max * 5 / 100;
        ItemStack repaired{filled[0]->item, 1};
        repaired.damage = std::max(0, max - remaining);
        return {repaired, nullptr};
    }
    return {};
}

const Recipe* RecipeBook::findCooking(RecipeType type, const ItemStack& input) const {
    if (input.empty() || type == RecipeType::Shaped || type == RecipeType::Shapeless) return nullptr;
    const auto& list = cookingByItem_[cookingIndex(type)];
    if (input.item->index >= list.size() || list[input.item->index].empty()) return nullptr;
    return list[input.item->index].front();
}

ItemStack RecipeBook::remainder(const ItemStack& used) const {
    if (used.empty() || !items_) return {};
    const std::string_view id = shortId(used.item->id);
    const char* left = id == "water_bucket" || id == "lava_bucket" || id == "milk_bucket" || id == "powder_snow_bucket"
                           ? "bucket"
                       : id == "honey_bottle" || id == "dragon_breath" ? "glass_bottle"
                                                                       : nullptr;
    const Item* item = left ? items_->find(left) : nullptr;
    return item ? ItemStack{item, 1} : ItemStack{};
}

std::vector<std::string> RecipeBook::conflicts() const {
    // Each recipe's simplest grid (the first item of every ingredient, mirrored too): any
    // other recipe that matches it with a different result conflicts with it.
    std::vector<std::string> out;
    std::set<std::pair<const Recipe*, const Recipe*>> seen;
    for (const Recipe& r : recipes_) {
        if (!r.crafting()) continue;
        std::array<ItemStack, 9> grid{};
        if (r.type == RecipeType::Shaped) {
            for (int y = 0; y < r.height; ++y)
                for (int x = 0; x < r.width; ++x) {
                    const Ingredient& ing = r.ingredients[size_t(y * r.width + x)];
                    if (!ing.empty()) grid[size_t(y * 3 + x)] = {ing.items.front(), 1};
                }
        } else {
            for (size_t i = 0; i < r.ingredients.size(); ++i) grid[i] = {r.ingredients[i].items.front(), 1};
        }
        const Trimmed t = trim(grid, 3, 3);
        for (const Recipe& o : recipes_) {
            if (&o == &r || !o.crafting() || (o.result.item == r.result.item && o.result.count == r.result.count)) continue;
            bool hit = false;
            if (o.type == RecipeType::Shaped)
                hit = o.width == t.width && o.height == t.height &&
                      (matchesShaped(o, grid, 3, t, false) || matchesShaped(o, grid, 3, t, true));
            else
                hit = int(o.ingredients.size()) == t.count && matchesShapeless(o, grid);
            if (hit && seen.insert({std::min(&r, &o), std::max(&r, &o)}).second)
                out.push_back(std::format("{} ({}) and {} ({})", r.source, r.result.item->id, o.source, o.result.item->id));
        }
    }
    return out;
}

} // namespace mc
