#include "resources/BlockStateDef.h"

#include <algorithm>
#include <format>

#include "resources/ResourcePack.h"

namespace mc {
namespace {

std::vector<std::string> splitPipes(std::string_view s) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= s.size()) {
        const size_t bar = s.find('|', pos);
        const size_t end = bar == std::string_view::npos ? s.size() : bar;
        out.emplace_back(s.substr(pos, end - pos));
        if (bar == std::string_view::npos) break;
        pos = bar + 1;
    }
    return out;
}

std::string jsonScalarToString(const Json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    return v.dump();
}

bool parseChoice(const Json& j, VariantChoice& out, std::string* error) {
    auto one = [&](const Json& v) -> bool {
        if (!v.is_object() || !v.contains("model")) {
            if (error) *error = "variant without a model";
            return false;
        }
        ModelVariant m;
        m.model = canonicalId(v["model"].get<std::string>());
        m.x = ((jsonGet<int>(v, "x", 0) % 360) + 360) % 360;
        m.y = ((jsonGet<int>(v, "y", 0) % 360) + 360) % 360;
        m.uvlock = jsonGet<bool>(v, "uvlock", false);
        m.weight = std::max(1, jsonGet<int>(v, "weight", 1));
        out.totalWeight += m.weight;
        out.options.push_back(std::move(m));
        return true;
    };
    if (j.is_array()) {
        for (const auto& v : j)
            if (!one(v)) return false;
        if (out.options.empty()) {
            if (error) *error = "empty variant list";
            return false;
        }
        return true;
    }
    return one(j);
}

bool parseCondition(const Json& j, Condition& out, std::string* error) {
    if (!j.is_object()) {
        if (error) *error = "condition is not an object";
        return false;
    }
    for (const char* key : {"OR", "AND"}) {
        if (auto it = j.find(key); it != j.end()) {
            if (j.size() != 1 || !it->is_array()) {
                if (error) *error = std::format("{} must be the only key and hold an array", key);
                return false;
            }
            out.kind = key[0] == 'O' ? Condition::Kind::Or : Condition::Kind::And;
            for (const auto& c : *it) {
                Condition child;
                if (!parseCondition(c, child, error)) return false;
                out.children.push_back(std::move(child));
            }
            return true;
        }
    }
    out.kind = Condition::Kind::Props;
    for (auto it = j.begin(); it != j.end(); ++it) {
        Condition::Test t;
        t.property = it.key();
        std::string value = jsonScalarToString(*it);
        if (value.starts_with('!')) {
            t.negate = true;
            value.erase(0, 1);
        }
        t.values = splitPipes(value);
        out.tests.push_back(std::move(t));
    }
    return true;
}

void collect(const Condition& c, std::vector<std::pair<std::string, std::vector<std::string>>>& out) {
    if (c.kind != Condition::Kind::Props) {
        for (const auto& ch : c.children) collect(ch, out);
        return;
    }
    for (const auto& t : c.tests) {
        auto it = std::find_if(out.begin(), out.end(), [&](const auto& p) { return p.first == t.property; });
        if (it == out.end()) {
            out.emplace_back(t.property, std::vector<std::string>{});
            it = std::prev(out.end());
        }
        for (const auto& v : t.values)
            if (std::find(it->second.begin(), it->second.end(), v) == it->second.end()) it->second.push_back(v);
    }
}

} // namespace

PropList parseVariantKey(std::string_view key) {
    PropList props;
    size_t pos = 0;
    while (pos < key.size()) {
        const size_t comma = key.find(',', pos);
        const size_t end = comma == std::string_view::npos ? key.size() : comma;
        const std::string_view pair = key.substr(pos, end - pos);
        if (const size_t eq = pair.find('='); eq != std::string_view::npos)
            props.emplace_back(std::string(pair.substr(0, eq)), std::string(pair.substr(eq + 1)));
        if (comma == std::string_view::npos) break;
        pos = comma + 1;
    }
    std::sort(props.begin(), props.end());
    return props;
}

std::optional<BlockStateDef> BlockStateDef::parse(std::string block, const Json& j, std::string* error) {
    BlockStateDef def;
    def.block = std::move(block);
    std::string err;
    if (auto v = j.find("variants"); v != j.end() && v->is_object()) {
        for (auto it = v->begin(); it != v->end(); ++it) {
            Variant var;
            var.props = parseVariantKey(it.key());
            if (!parseChoice(*it, var.choice, &err)) {
                if (error) *error = std::format("{} variant '{}': {}", def.block, it.key(), err);
                return std::nullopt;
            }
            def.variants.push_back(std::move(var));
        }
        return def;
    }
    if (auto m = j.find("multipart"); m != j.end() && m->is_array()) {
        def.multipart = true;
        for (const auto& p : *m) {
            Part part;
            if (auto w = p.find("when"); w != p.end()) {
                part.always = false;
                if (!parseCondition(*w, part.when, &err)) {
                    if (error) *error = std::format("{} multipart condition: {}", def.block, err);
                    return std::nullopt;
                }
            }
            if (!p.contains("apply") || !parseChoice(p["apply"], part.apply, &err)) {
                if (error) *error = std::format("{} multipart apply: {}", def.block, err.empty() ? "missing" : err);
                return std::nullopt;
            }
            def.parts.push_back(std::move(part));
        }
        return def;
    }
    if (error) *error = std::format("{}: neither variants nor multipart", def.block);
    return std::nullopt;
}

std::vector<std::pair<std::string, std::vector<std::string>>> BlockStateDef::mentionedProperties() const {
    std::vector<std::pair<std::string, std::vector<std::string>>> out;
    if (!multipart) {
        for (const auto& v : variants)
            for (const auto& [k, val] : v.props) {
                auto it = std::find_if(out.begin(), out.end(), [&](const auto& p) { return p.first == k; });
                if (it == out.end()) {
                    out.emplace_back(k, std::vector<std::string>{});
                    it = std::prev(out.end());
                }
                if (std::find(it->second.begin(), it->second.end(), val) == it->second.end()) it->second.push_back(val);
            }
        return out;
    }
    for (const auto& p : parts)
        if (!p.always) collect(p.when, out);
    for (auto& [name, values] : out) {
        const bool boolean = std::all_of(values.begin(), values.end(), [](const std::string& v) {
            return v == "true" || v == "false";
        });
        if (boolean) {
            values = {"true", "false"};
            continue;
        }
        const bool side = std::any_of(values.begin(), values.end(), [](const std::string& v) {
            return v == "side" || v == "up" || v == "low" || v == "tall";
        });
        if (side && std::find(values.begin(), values.end(), "none") == values.end()) values.insert(values.begin(), "none");
    }
    return out;
}

} // namespace mc
