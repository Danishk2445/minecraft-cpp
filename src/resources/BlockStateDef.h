// BlockStateDef.h -- blockstates/<block>.json: variants and multipart selection.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/Json.h"

namespace mc {

struct ModelVariant {
    std::string model; // canonical model id
    int x = 0;         // blockstate rotation in degrees (0/90/180/270)
    int y = 0;
    bool uvlock = false;
    int weight = 1;
};

// A weighted random choice between variants (a single entry for most blocks).
struct VariantChoice {
    std::vector<ModelVariant> options;
    int totalWeight = 0;
};

using PropList = std::vector<std::pair<std::string, std::string>>;

struct Condition {
    enum class Kind { Props, And, Or };
    Kind kind = Kind::Props;
    struct Test {
        std::string property;
        std::vector<std::string> values; // "a|b" alternatives
        bool negate = false;             // "!a"
    };
    std::vector<Test> tests; // Props: all must hold
    std::vector<Condition> children;

    // `get(name)` returns the state's value for a property ("" if the block lacks it).
    template <class Lookup>
    bool matches(const Lookup& get) const {
        switch (kind) {
        case Kind::And:
            for (const auto& c : children)
                if (!c.matches(get)) return false;
            return true;
        case Kind::Or:
            for (const auto& c : children)
                if (c.matches(get)) return true;
            return false;
        case Kind::Props:
            for (const auto& t : tests) {
                const std::string_view v = get(std::string_view(t.property));
                bool hit = false;
                for (const auto& want : t.values)
                    if (v == want) hit = true;
                if (hit == t.negate) return false;
            }
            return true;
        }
        return false;
    }
};

struct BlockStateDef {
    std::string block; // canonical block id
    bool multipart = false;

    struct Variant {
        PropList props; // from the key, sorted by name
        VariantChoice choice;
    };
    std::vector<Variant> variants;

    struct Part {
        bool always = true;
        Condition when;
        VariantChoice apply;
    };
    std::vector<Part> parts;

    static std::optional<BlockStateDef> parse(std::string block, const Json& j, std::string* error);

    // Every property and value the file mentions. For multipart files the domains
    // are completed: boolean properties get both values and connection sides
    // ("side", "up", "low", "tall") get "none". Values are in first-seen order.
    std::vector<std::pair<std::string, std::vector<std::string>>> mentionedProperties() const;

    // Appends the choices that apply to a state.
    template <class Lookup>
    void select(const Lookup& get, std::vector<const VariantChoice*>& out) const {
        if (!multipart) {
            for (const auto& v : variants) {
                bool ok = true;
                for (const auto& [k, val] : v.props)
                    if (get(std::string_view(k)) != val) {
                        ok = false;
                        break;
                    }
                if (ok) {
                    out.push_back(&v.choice);
                    return;
                }
            }
            return;
        }
        for (const auto& p : parts)
            if (p.always || p.when.matches(get)) out.push_back(&p.apply);
    }
};

// Parses "facing=east,half=bottom" (sorted by property name).
PropList parseVariantKey(std::string_view key);

} // namespace mc
