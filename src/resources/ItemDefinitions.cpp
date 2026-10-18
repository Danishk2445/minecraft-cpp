#include "resources/ItemDefinitions.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <format>

#include "core/Json.h"
#include "resources/ModelBaker.h"
#include "resources/ResourcePack.h"
#include "world/block/Tints.h"

namespace mc {

struct ItemDefinitions::Node {
    enum class Type : uint8_t { Model, Condition, Select, RangeDispatch, Special, Composite, Empty, BundleSelected };
    Type type = Type::Empty;
    const BakedModel* model = nullptr;     // Model; Special: its base
    std::vector<ItemTintSource> tints;     // Model
    SpecialModelSpec special;              // Special
    std::string property;                  // Condition, Select, RangeDispatch
    std::string argument;                  // pattern, block_state_property, component, target or source
    float scale = 1.0f;                    // RangeDispatch
    std::unique_ptr<Node> onTrue, onFalse; // Condition
    std::vector<std::pair<std::vector<std::string>, std::unique_ptr<Node>>> cases; // Select
    std::vector<std::pair<float, std::unique_ptr<Node>>> entries;                  // RangeDispatch, ascending
    std::unique_ptr<Node> fallback;        // Select, RangeDispatch (null: the missing model)
    std::vector<std::unique_ptr<Node>> children; // Composite
};

struct ItemDefinitions::Impl {
    std::unordered_map<std::string, std::unique_ptr<Node>> items;
    const BakedModel* missing = nullptr;
};

namespace {

using Node = ItemDefinitions::Node;

constexpr std::string_view kDisplayNames[] = {"thirdperson_righthand", "thirdperson_lefthand", "firstperson_righthand",
                                              "firstperson_lefthand",  "head",                 "gui",
                                              "ground",                "fixed"};

uint32_t argbOf(const Json& v, uint32_t fallback) {
    if (v.is_number_integer()) return uint32_t(v.get<int64_t>());
    if (v.is_array() && v.size() >= 3) {
        // [r, g, b] floats
        auto c = [&](size_t i) { return uint32_t(std::clamp(v[i].get<double>(), 0.0, 1.0) * 255.0 + 0.5); };
        return 0xFF000000u | (c(0) << 16) | (c(1) << 8) | c(2);
    }
    return fallback;
}

ItemTintSource parseTint(const Json& j, std::string& problems, const std::string& where) {
    ItemTintSource t;
    const std::string type = canonicalId(j.value("type", std::string()));
    using T = ItemTintSource::Type;
    if (type == "minecraft:constant") t.type = T::Constant, t.value = argbOf(j.value("value", Json(-1)), 0xFFFFFFFF);
    else if (type == "minecraft:dye") t.type = T::Dye;
    else if (type == "minecraft:grass") t.type = T::Grass;
    else if (type == "minecraft:firework") t.type = T::Firework;
    else if (type == "minecraft:potion") t.type = T::Potion;
    else if (type == "minecraft:map_color") t.type = T::MapColor;
    else if (type == "minecraft:team") t.type = T::Team;
    else if (type == "minecraft:custom_model_data") t.type = T::CustomModelData;
    else problems += std::format("{}: unknown tint {}\n", where, type);
    if (t.type != T::Constant) t.value = argbOf(j.value("default", Json(-1)), 0xFFFFFFFF);
    t.temperature = j.value("temperature", 0.5f);
    t.downfall = j.value("downfall", 1.0f);
    return t;
}

std::unique_ptr<Node> parseNode(const Json& j, ModelBaker& baker, const std::string& where, std::string& problems) {
    auto n = std::make_unique<Node>();
    if (!j.is_object()) {
        problems += std::format("{}: expected an object\n", where);
        return n;
    }
    const std::string type = canonicalId(j.value("type", std::string()));
    auto child = [&](const char* key) -> std::unique_ptr<Node> {
        auto it = j.find(key);
        return it == j.end() ? nullptr : parseNode(*it, baker, where, problems);
    };
    if (type == "minecraft:model") {
        n->type = Node::Type::Model;
        n->model = baker.bake(ModelVariant{canonicalId(j.value("model", std::string()))});
        for (const Json& t : j.value("tints", Json::array())) n->tints.push_back(parseTint(t, problems, where));
    } else if (type == "minecraft:special") {
        n->type = Node::Type::Special;
        n->model = baker.bake(ModelVariant{canonicalId(j.value("base", std::string()))});
        const Json m = j.value("model", Json::object());
        const std::string kind = canonicalId(m.value("type", std::string()));
        static const std::pair<std::string_view, SpecialModel> kKinds[] = {
            {"minecraft:bed", SpecialModel::Bed},
            {"minecraft:banner", SpecialModel::Banner},
            {"minecraft:chest", SpecialModel::Chest},
            {"minecraft:conduit", SpecialModel::Conduit},
            {"minecraft:decorated_pot", SpecialModel::DecoratedPot},
            {"minecraft:head", SpecialModel::Head},
            {"minecraft:player_head", SpecialModel::PlayerHead},
            {"minecraft:shield", SpecialModel::Shield},
            {"minecraft:shulker_box", SpecialModel::ShulkerBox},
            {"minecraft:trident", SpecialModel::Trident},
        };
        bool known = false;
        for (const auto& [name, value] : kKinds)
            if (kind == name) n->special.type = value, known = true;
        if (!known) {
            problems += std::format("{}: unknown special model {}\n", where, kind);
            n->type = Node::Type::Model; // draw the base model instead
        }
        n->special.texture = m.value("texture", std::string());
        n->special.kind = m.value("kind", std::string());
        n->special.color = m.value("color", std::string());
    } else if (type == "minecraft:condition") {
        n->type = Node::Type::Condition;
        n->property = canonicalId(j.value("property", std::string()));
        n->argument = j.value("component", std::string());
        n->onTrue = child("on_true");
        n->onFalse = child("on_false");
    } else if (type == "minecraft:select") {
        n->type = Node::Type::Select;
        n->property = canonicalId(j.value("property", std::string()));
        n->argument = j.contains("pattern")                ? j.value("pattern", std::string())
                      : j.contains("block_state_property") ? j.value("block_state_property", std::string())
                                                           : j.value("component", std::string());
        // Armor trims never apply (their sprites only exist as paletted permutations),
        // so their cases are neither parsed nor baked.
        const bool trims = n->property == "minecraft:trim_material";
        for (const Json& c : trims ? Json::array() : j.value("cases", Json::array())) {
            std::vector<std::string> when;
            const Json w = c.value("when", Json());
            if (w.is_string()) when.push_back(w.get<std::string>());
            for (const Json& v : w.is_array() ? w : Json::array())
                if (v.is_string()) when.push_back(v.get<std::string>());
            n->cases.emplace_back(std::move(when), parseNode(c.value("model", Json::object()), baker, where, problems));
        }
        n->fallback = child("fallback");
    } else if (type == "minecraft:range_dispatch") {
        n->type = Node::Type::RangeDispatch;
        n->property = canonicalId(j.value("property", std::string()));
        n->argument = j.contains("target") ? j.value("target", std::string()) : j.value("source", std::string());
        n->scale = j.value("scale", 1.0f);
        for (const Json& e : j.value("entries", Json::array()))
            n->entries.emplace_back(e.value("threshold", 0.0f),
                                    parseNode(e.value("model", Json::object()), baker, where, problems));
        std::stable_sort(n->entries.begin(), n->entries.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
        n->fallback = child("fallback");
    } else if (type == "minecraft:composite") {
        n->type = Node::Type::Composite;
        for (const Json& m : j.value("models", Json::array())) n->children.push_back(parseNode(m, baker, where, problems));
    } else if (type == "minecraft:bundle/selected_item") {
        n->type = Node::Type::BundleSelected;
    } else if (type != "minecraft:empty") {
        problems += std::format("{}: unknown item model type {}\n", where, type);
    }
    return n;
}

void collectModels(const Node& n, std::vector<const BakedModel*>& out) {
    if (n.model) out.push_back(n.model);
    for (const Node* c : {n.onTrue.get(), n.onFalse.get(), n.fallback.get()})
        if (c) collectModels(*c, out);
    for (const auto& [when, c] : n.cases) collectModels(*c, out);
    for (const auto& [threshold, c] : n.entries) collectModels(*c, out);
    for (const auto& c : n.children) collectModels(*c, out);
}

// The current local date and time formatted with a Java-style pattern (yyyy MM dd HH mm ss).
std::string localTime(std::string_view pattern) {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    localtime_r(&now, &tm);
    std::string out;
    for (size_t i = 0; i < pattern.size();) {
        size_t run = 1;
        while (i + run < pattern.size() && pattern[i + run] == pattern[i]) ++run;
        const char c = pattern[i];
        const int value = c == 'y' ? tm.tm_year + 1900 : c == 'M' ? tm.tm_mon + 1 : c == 'd' ? tm.tm_mday
                        : c == 'H' ? tm.tm_hour : c == 'm' ? tm.tm_min : c == 's' ? tm.tm_sec : -1;
        if (value < 0) out.append(run, c);
        else out += std::format("{:0{}}", value, run);
        i += run;
    }
    return out;
}

bool condition(const Node& n, const ItemModelContext& ctx) {
    if (n.property == "minecraft:using_item") return ctx.usingItem;
    // broken, damaged, has_component, bundle/has_selected_item, fishing_rod/cast, selected,
    // carried, extended_view, keybind_down, view_entity, custom_model_data: no data yet.
    return false;
}

std::string selectValue(const Node& n, const ItemModelContext& ctx) {
    if (n.property == "minecraft:display_context") return std::string(displayContextName(ctx.display));
    if (n.property == "minecraft:local_time") return localTime(n.argument.empty() ? "yyyy-MM-dd" : n.argument);
    if (n.property == "minecraft:context_dimension") return ctx.overworld ? "minecraft:overworld" : "";
    if (n.property == "minecraft:main_hand") return "right";
    if (n.property == "minecraft:charge_type") return "none";
    if (n.property == "minecraft:context_entity_type") return "minecraft:player";
    return ""; // block_state, trim_material, component, custom_model_data: no data yet
}

float positiveModulo(float a, float b) {
    const float m = std::fmod(a, b);
    return m < 0.0f ? m + b : m;
}

float rangeValue(const Node& n, const ItemModelContext& ctx) {
    if (n.property == "minecraft:time")
        return n.argument == "daytime" && ctx.overworld ? ctx.timeOfDay : ctx.spin;
    if (n.property == "minecraft:compass") {
        // Vanilla CompassAngleState: from the holder toward the target's block center.
        if (n.argument != "spawn" || !ctx.overworld || !ctx.holderPos) return ctx.spin;
        constexpr double kTau = 6.283185307179586;
        const glm::dvec3& p = *ctx.holderPos;
        const double toTarget = std::atan2(ctx.spawnPos.z + 0.5 - p.z, ctx.spawnPos.x + 0.5 - p.x) / kTau;
        const double yaw = std::fmod(std::fmod(double(ctx.holderYaw) / 360.0, 1.0) + 1.0, 1.0);
        return positiveModulo(float(0.5 - (yaw - 0.25 - toTarget)), 1.0f);
    }
    if (n.property == "minecraft:count") return float(ctx.count) / float(std::max(ctx.maxStack, 1));
    if (n.property == "minecraft:use_duration") return ctx.usingItem ? float(ctx.useDuration) : 0.0f;
    return 0.0f; // damage, cooldown, crossbow/pull, use_cycle, bundle/fullness
}

uint32_t resolveTint(const ItemTintSource& t, const Colormap* grass) {
    if (t.type == ItemTintSource::Type::Grass)
        return 0xFF000000u | (grass && grass->valid() ? grass->sample(t.temperature, t.downfall) : 0x91BD59u);
    return 0xFF000000u | (t.value & 0xFFFFFF); // no item data: the default
}

void evaluateNode(const Node& n, const ItemModelContext& ctx, const Colormap* grass, const BakedModel* missing,
                  std::vector<ItemLayer>& out) {
    auto missingLayer = [&] {
        ItemLayer l;
        l.model = l.transforms = missing;
        out.push_back(l);
    };
    switch (n.type) {
    case Node::Type::Model: {
        ItemLayer l;
        l.model = l.transforms = n.model;
        l.tintCount = int(std::min<size_t>(n.tints.size(), l.tints.size()));
        for (int i = 0; i < l.tintCount; ++i) l.tints[size_t(i)] = resolveTint(n.tints[size_t(i)], grass);
        out.push_back(l);
        return;
    }
    case Node::Type::Special: {
        ItemLayer l;
        l.transforms = n.model;
        l.special = &n.special;
        out.push_back(l);
        return;
    }
    case Node::Type::Condition: {
        const Node* next = condition(n, ctx) ? n.onTrue.get() : n.onFalse.get();
        if (next) evaluateNode(*next, ctx, grass, missing, out);
        else missingLayer();
        return;
    }
    case Node::Type::Select: {
        const std::string value = selectValue(n, ctx);
        for (const auto& [when, node] : n.cases)
            if (std::find(when.begin(), when.end(), value) != when.end()) {
                evaluateNode(*node, ctx, grass, missing, out);
                return;
            }
        if (n.fallback) evaluateNode(*n.fallback, ctx, grass, missing, out);
        else missingLayer();
        return;
    }
    case Node::Type::RangeDispatch: {
        // The last entry whose threshold is at most the scaled value (vanilla RangeSelectItemModel).
        const float value = rangeValue(n, ctx) * n.scale;
        const Node* pick = n.fallback.get();
        bool found = false;
        if (!std::isnan(value))
            for (const auto& [threshold, node] : n.entries)
                if (threshold <= value) pick = node.get(), found = true;
        if (pick) evaluateNode(*pick, ctx, grass, missing, out);
        else if (!found) missingLayer();
        return;
    }
    case Node::Type::Composite:
        for (const auto& c : n.children) evaluateNode(*c, ctx, grass, missing, out);
        return;
    case Node::Type::Empty:
    case Node::Type::BundleSelected: // bundles have no contents yet
        return;
    }
}

} // namespace

std::string_view displayContextName(DisplayContext c) {
    const size_t i = size_t(c);
    return i < std::size(kDisplayNames) ? kDisplayNames[i] : "none";
}

ItemDefinitions::ItemDefinitions() : impl_(std::make_unique<Impl>()) {}
ItemDefinitions::~ItemDefinitions() = default;
ItemDefinitions::ItemDefinitions(ItemDefinitions&&) noexcept = default;
ItemDefinitions& ItemDefinitions::operator=(ItemDefinitions&&) noexcept = default;

bool ItemDefinitions::load(const ResourcePack& pack, ModelBaker& baker, std::string* problems) {
    impl_->items.clear();
    impl_->missing = baker.missingModel();
    std::string report;
    std::error_code ec;
    const std::filesystem::path dir = pack.file("minecraft", "items");
    std::vector<std::filesystem::path> files;
    for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::end(it); it.increment(ec))
        if (it->path().extension() == ".json") files.push_back(it->path());
    std::sort(files.begin(), files.end());
    for (const auto& file : files) {
        const std::string id = "minecraft:" + file.stem().string();
        std::string err;
        auto doc = pack.json(file, &err);
        if (!doc || !doc->is_object() || !doc->contains("model")) {
            report += std::format("{}: {}\n", id, err.empty() ? "no \"model\"" : err);
            continue;
        }
        impl_->items.emplace(id, parseNode((*doc)["model"], baker, id, report));
    }
    if (problems) *problems = report;
    return !impl_->items.empty();
}

size_t ItemDefinitions::size() const { return impl_->items.size(); }

bool ItemDefinitions::has(std::string_view itemId) const { return impl_->items.contains(std::string(itemId)); }

std::vector<std::string> ItemDefinitions::ids() const {
    std::vector<std::string> out;
    out.reserve(impl_->items.size());
    for (const auto& [id, node] : impl_->items) out.push_back(id);
    std::sort(out.begin(), out.end());
    return out;
}

void ItemDefinitions::evaluate(std::string_view itemId, const ItemModelContext& ctx, const Colormap* grass,
                               std::vector<ItemLayer>& out) const {
    auto it = impl_->items.find(std::string(itemId));
    if (it == impl_->items.end()) {
        ItemLayer l;
        l.model = l.transforms = impl_->missing;
        out.push_back(l);
        return;
    }
    evaluateNode(*it->second, ctx, grass, impl_->missing, out);
}

std::vector<const BakedModel*> ItemDefinitions::allModels(std::string_view itemId) const {
    std::vector<const BakedModel*> out;
    if (auto it = impl_->items.find(std::string(itemId)); it != impl_->items.end()) collectModels(*it->second, out);
    return out;
}

} // namespace mc
