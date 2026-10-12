#include "game/Loot.h"

#include <format>
#include <functional>

#include "core/Json.h"
#include "core/TagSet.h"
#include "resources/ResourcePack.h"

namespace mc {

bool BlockLoot::load(const std::filesystem::path& file, const BlockRegistry& blocks, const ItemRegistry& items,
                     std::string* error) {
    blocks_ = &blocks;
    items_ = &items;
    rules_.clear();
    ruleOfState_.assign(blocks.stateCount(), -1);
    std::vector<std::string> problems;
    std::string err;
    auto doc = loadJsonFile(file, &err);
    if (!doc || !doc->is_object()) {
        if (error) *error = err.empty() ? file.string() + ": expected an object" : err;
        return false;
    }
    const Json rules = doc->value("rules", Json::array());

    std::function<Drop(const Json&, size_t)> parseDrop;
    auto parseDrops = [&](const Json& list, std::vector<Drop>& out, size_t ruleIndex) {
        for (const Json& d : list) out.push_back(parseDrop(d, ruleIndex));
    };
    parseDrop = [&](const Json& d, size_t ruleIndex) {
        Drop drop;
        const std::string id = d.value("item", std::string());
        if (id != "self") {
            drop.item = items.find(id);
            if (!drop.item) problems.push_back(std::format("loot rule {}: unknown item {}", ruleIndex, id));
        }
        const Json count = d.value("count", Json());
        if (count.is_number_integer()) drop.count.min = drop.count.max = count.get<int>();
        else if (count.is_array() && count.size() == 2)
            drop.count.min = count[0].get<int>(), drop.count.max = count[1].get<int>();
        else if (count.is_object()) {
            const Json b = count.value("binomial", Json::array());
            if (b.is_array() && b.size() == 2) {
                drop.count.binomialN = b[0].get<int>();
                drop.count.binomialP = b[1].get<float>();
            }
            drop.count.min = drop.count.max = count.value("plus", 0);
        }
        drop.count.fromProperty = d.value("count_from", std::string());
        drop.chance = d.value("chance", 1.0f);
        if (const Json other = d.value("else", Json()); other.is_object())
            drop.otherwise = std::make_shared<Drop>(parseDrop(other, ruleIndex));
        return drop;
    };

    for (size_t i = 0; i < rules.size(); ++i) {
        const Json& r = rules[i];
        if (!r.contains("match")) continue; // a comment-only entry
        Rule rule;
        parseDrops(r.value("drops", Json::array()), rule.drops, i);
        if (r.contains("shears")) {
            rule.hasShears = true;
            parseDrops(r.value("shears", Json::array()), rule.shears, i);
        }
        rule.playerOnly = r.value("player_only", false);
        if (const Json xp = r.value("xp", Json::array()); xp.is_array() && xp.size() == 2) {
            rule.xpMin = xp[0].get<int>();
            rule.xpMax = xp[1].get<int>();
        }
        const int16_t index = int16_t(rules_.size());
        rules_.push_back(std::move(rule));
        // The blocks it matches, then the states whose properties fit "when".
        std::vector<bool> matched(blocks.blockCount(), false);
        const Json match = r.value("match", Json::array());
        for (const Json& m : match) {
            const std::string entry = m.get<std::string>();
            bool any = false;
            if (entry.starts_with('#')) {
                if (const auto tag = blocks.tags().find(entry))
                    for (uint16_t b : blocks.tags().blocks(*tag)) matched[b] = any = true;
            } else {
                for (size_t b = 0; b < blocks.blockCount(); ++b)
                    if (globMatch(entry, shortId(blocks.block(b).id))) matched[b] = any = true;
            }
            if (!any) problems.push_back(std::format("loot rule {}: {} matches no block", i, entry));
        }
        const Json when = r.value("when", Json::object());
        for (size_t b = 0; b < blocks.blockCount(); ++b) {
            if (!matched[b]) continue;
            const Block& block = blocks.block(b);
            for (uint32_t local = 0; local < block.stateCount; ++local) {
                const StateId s = block.stateAt(local);
                if (ruleOfState_[s] >= 0) continue; // an earlier rule wins
                bool fits = true;
                for (const auto& [prop, want] : when.items()) {
                    const std::string_view v = blocks.value(s, prop);
                    if (want.is_array()) {
                        bool any = false;
                        for (const Json& w : want) any = any || (w.is_string() && v == w.get<std::string>());
                        fits = fits && any;
                    } else {
                        fits = fits && want.is_string() && v == want.get<std::string>();
                    }
                }
                if (fits) ruleOfState_[s] = index;
            }
        }
    }
    if (error) {
        error->clear();
        for (const auto& p : problems) *error += p + "\n";
    }
    return problems.empty();
}

void BlockLoot::rollOne(const Drop& d, StateId state, Xoroshiro& random, std::vector<ItemStack>& out) const {
    if (d.chance < 1.0f && random.nextFloat() >= d.chance) {
        if (d.otherwise) rollOne(*d.otherwise, state, random, out);
        return;
    }
    const Item* item = d.item ? d.item : items_->forBlock(blocks_->blockOf(state));
    if (!item) return;
    int n = d.count.min + (d.count.max > d.count.min ? random.nextInt(d.count.max - d.count.min + 1) : 0);
    for (int k = 0; k < d.count.binomialN; ++k)
        if (random.nextFloat() < d.count.binomialP) ++n;
    if (!d.count.fromProperty.empty()) n = blocks_->intValue(state, d.count.fromProperty, n);
    while (n > 0) {
        const int take = std::min(n, item->maxStack);
        out.push_back({item, take});
        n -= take;
    }
}

std::vector<ItemStack> BlockLoot::roll(const std::vector<Drop>& drops, StateId state, Xoroshiro& random) const {
    std::vector<ItemStack> out;
    for (const Drop& d : drops) rollOne(d, state, random, out);
    return out;
}

bool BlockLoot::dropsWhenUpdated(StateId state) const {
    const int16_t r = state < ruleOfState_.size() ? ruleOfState_[state] : -1;
    return r < 0 || !rules_[size_t(r)].playerOnly;
}

std::vector<ItemStack> BlockLoot::drops(StateId state, bool shears, Xoroshiro& random) const {
    const int16_t r = state < ruleOfState_.size() ? ruleOfState_[state] : -1;
    if (r < 0) {
        const Item* own = items_ ? items_->forBlock(blocks_->blockOf(state)) : nullptr;
        if (!own) return {};
        return {ItemStack{own, 1}};
    }
    const Rule& rule = rules_[size_t(r)];
    return roll(shears && rule.hasShears ? rule.shears : rule.drops, state, random);
}

int BlockLoot::experience(StateId state, Xoroshiro& random) const {
    const int16_t r = state < ruleOfState_.size() ? ruleOfState_[state] : -1;
    if (r < 0) return 0;
    const Rule& rule = rules_[size_t(r)];
    if (rule.xpMax <= 0) return 0;
    return rule.xpMin + (rule.xpMax > rule.xpMin ? random.nextInt(rule.xpMax - rule.xpMin + 1) : 0);
}

} // namespace mc
