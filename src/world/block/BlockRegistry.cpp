#include "world/block/BlockRegistry.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <functional>

#include "core/Json.h"
#include "core/Log.h"
#include "resources/Atlas.h"
#include "resources/BlockModels.h"
#include "resources/ResourcePack.h"

namespace mc {
namespace {

// ---------------------------------------------------------------- schemas
//
// Property schemas of the behavior classes, in a compact form:
//   name:v1,v2,v3[=default]   enum
//   name:bool[=true]          false/true
//   name:LO-HI[=default]      integer range
//   name:h                    horizontal facing (north default)
//   name:6[=default]          six-way facing (north default)
// "$param" is replaced by the class parameters from classes.json.
const std::unordered_map<std::string_view, std::string_view>& classSchemas() {
    static const std::unordered_map<std::string_view, std::string_view> table = {
        {"air", ""},
        {"simple", ""},
        {"stairs", "facing:h half:top,bottom=bottom "
                   "shape:straight,inner_left,inner_right,outer_left,outer_right waterlogged:bool"},
        {"slab", "type:top,bottom,double=bottom waterlogged:bool"},
        {"fence", "north:bool east:bool south:bool west:bool waterlogged:bool"},
        {"pane", "north:bool east:bool south:bool west:bool waterlogged:bool"},
        {"wall", "up:bool=true north:none,low,tall east:none,low,tall south:none,low,tall west:none,low,tall "
                 "waterlogged:bool"},
        {"fence_gate", "facing:h in_wall:bool open:bool powered:bool"},
        {"door", "facing:h half:upper,lower=lower hinge:left,right open:bool powered:bool"},
        {"trapdoor", "facing:h half:top,bottom=bottom open:bool powered:bool waterlogged:bool"},
        {"button", "face:floor,wall,ceiling=wall facing:h powered:bool"},
        {"lever", "face:floor,wall,ceiling=wall facing:h powered:bool"},
        {"pressure_plate", "powered:bool"},
        {"weighted_pressure_plate", "power:0-15"},
        {"torch", ""},
        {"wall_torch", "facing:h"},
        {"redstone_torch", "lit:bool=true"},
        {"redstone_wall_torch", "facing:h lit:bool=true"},
        {"leaves", "distance:1-7=7 persistent:bool waterlogged:bool"},
        {"pillar", "axis:x,y,z=y"},
        {"crop", "age:0-$max"},
        {"sapling", "stage:0-1"},
        {"snowy", "snowy:bool"},
        {"farmland", "moisture:0-7"},
        {"cactus", "age:0-15"},
        {"sugar_cane", "age:0-15"},
        {"fire", "age:0-15 north:bool east:bool south:bool west:bool up:bool"},
        {"bed", "facing:h part:head,foot=foot occupied:bool"},
        {"chest", "facing:h type:single,left,right=single waterlogged:bool"},
        {"ender_chest", "facing:h waterlogged:bool"},
        {"furnace", "facing:h lit:bool"},
        {"barrel", "facing:6 open:bool"},
        {"sign", "rotation:0-15 waterlogged:bool"},
        {"wall_sign", "facing:h waterlogged:bool"},
        {"hanging_sign", "rotation:0-15 attached:bool waterlogged:bool"},
        {"wall_hanging_sign", "facing:h waterlogged:bool"},
        {"banner", "rotation:0-15"},
        {"wall_banner", "facing:h"},
        {"skull", "rotation:0-15 powered:bool"},
        {"wall_skull", "facing:h powered:bool"},
        {"ladder", "facing:h waterlogged:bool"},
        {"vine", "up:bool north:bool east:bool south:bool west:bool"},
        {"snow_layer", "layers:1-8"},
        {"liquid", "level:0-15"},
        {"tnt", "unstable:bool"},
        {"rail", "shape:north_south,east_west,ascending_east,ascending_west,ascending_north,ascending_south,"
                 "south_east,south_west,north_west,north_east waterlogged:bool"},
        {"powered_rail", "shape:north_south,east_west,ascending_east,ascending_west,ascending_north,ascending_south "
                         "powered:bool waterlogged:bool"},
        {"redstone_wire", "north:none,side,up east:none,side,up south:none,side,up west:none,side,up power:0-15"},
        {"repeater", "facing:h delay:1-4 locked:bool powered:bool"},
        {"comparator", "facing:h mode:compare,subtract powered:bool"},
        {"piston", "facing:6 extended:bool"},
        {"piston_head", "facing:6 short:bool type:normal,sticky"},
        {"observer", "facing:6=south powered:bool"},
        {"dispenser", "facing:6 triggered:bool"},
        {"hopper", "enabled:bool=true facing:down,north,south,west,east=down"},
        {"lantern", "hanging:bool waterlogged:bool"},
        {"campfire", "facing:h lit:bool=true signal_fire:bool waterlogged:bool"},
        {"candle", "candles:1-4 lit:bool waterlogged:bool"},
        {"candle_cake", "lit:bool"},
        {"cake", "bites:0-6"},
        {"kelp", "age:0-25"},
        {"water_plant", ""},
        {"tall_water_plant", "half:upper,lower=lower"},
        {"sea_pickle", "pickles:1-4 waterlogged:bool=true"},
        {"coral", "waterlogged:bool=true"},
        {"coral_wall_fan", "facing:h waterlogged:bool=true"},
        {"bamboo", "age:0-1 leaves:none,small,large stage:0-1"},
        {"berry_bush", "age:0-3"},
        {"cave_vines", "age:0-25 berries:bool"},
        {"cave_vines_plant", "berries:bool"},
        {"double_plant", "half:upper,lower=lower"},
        {"big_dripleaf", "facing:h tilt:none,unstable,partial,full waterlogged:bool"},
        {"big_dripleaf_stem", "facing:h waterlogged:bool"},
        {"small_dripleaf", "facing:h half:upper,lower=lower waterlogged:bool"},
        {"pointed_dripstone", "thickness:tip_merge,tip,frustum,middle,base=tip vertical_direction:up,down=up "
                              "waterlogged:bool"},
        {"amethyst", "facing:6=up waterlogged:bool"},
        {"lightning_rod", "facing:6=up powered:bool waterlogged:bool"},
        {"chain", "axis:x,y,z=y waterlogged:bool"},
        {"end_rod", "facing:6=up"},
        {"multiface", "down:bool up:bool north:bool south:bool west:bool east:bool waterlogged:bool"},
        {"mushroom_block", "down:bool=true up:bool=true north:bool=true south:bool=true west:bool=true "
                           "east:bool=true"},
        {"chorus_plant", "down:bool up:bool north:bool south:bool west:bool east:bool"},
        {"chorus_flower", "age:0-5"},
        {"bell", "attachment:floor,ceiling,single_wall,double_wall=floor facing:h powered:bool"},
        {"cocoa", "age:0-2 facing:h"},
        {"composter", "level:0-8"},
        {"layered_cauldron", "level:1-3"},
        {"scaffolding", "bottom:bool distance:0-7=7 waterlogged:bool"},
        {"turtle_egg", "eggs:1-4 hatch:0-2"},
        {"flowerbed", "facing:h flower_amount:1-4"},
        {"leaf_litter", "facing:h segment_amount:1-4"},
        {"stem", "age:0-7"},
        {"attached_stem", "facing:h"},
        {"propagule", "age:0-4 hanging:bool stage:0-1 waterlogged:bool"},
        {"nether_portal", "axis:x,z=x"},
        {"frosted_ice", "age:0-3"},
        {"light_block", "level:0-15=15 waterlogged:bool"},
        {"pitcher_crop", "age:0-4 half:upper,lower=lower"},
        {"waterloggable", "waterlogged:bool"},
    };
    return table;
}

struct SchemaProp {
    std::string name;
    std::vector<std::string> values;
    std::string defaultValue;
};

std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= s.size()) {
        const size_t n = s.find(sep, pos);
        const size_t end = n == std::string_view::npos ? s.size() : n;
        if (end > pos) out.emplace_back(s.substr(pos, end - pos));
        if (n == std::string_view::npos) break;
        pos = n + 1;
    }
    return out;
}

bool parseIntStr(std::string_view s, int& out) {
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc() && p == s.data() + s.size();
}

std::vector<SchemaProp> parseSchema(std::string spec, const Json& params) {
    if (params.is_object())
        for (auto it = params.begin(); it != params.end(); ++it) {
            const std::string key = "$" + it.key();
            const std::string val = it->is_string() ? it->get<std::string>() : it->dump();
            for (size_t p; (p = spec.find(key)) != std::string::npos;) spec.replace(p, key.size(), val);
        }
    std::vector<SchemaProp> out;
    for (const auto& token : split(spec, ' ')) {
        const size_t colon = token.find(':');
        SchemaProp prop;
        prop.name = token.substr(0, colon);
        std::string body = token.substr(colon + 1);
        if (const size_t eq = body.find('='); eq != std::string::npos) {
            prop.defaultValue = body.substr(eq + 1);
            body.resize(eq);
        }
        int lo, hi;
        const size_t dash = body.find('-');
        if (body == "bool") {
            prop.values = {"false", "true"};
        } else if (body == "h") {
            prop.values = {"north", "south", "west", "east"};
        } else if (body == "6") {
            prop.values = {"down", "up", "north", "south", "west", "east"};
            if (prop.defaultValue.empty()) prop.defaultValue = "north";
        } else if (dash != std::string::npos && parseIntStr(body.substr(0, dash), lo) &&
                   parseIntStr(body.substr(dash + 1), hi)) {
            for (int v = lo; v <= hi; ++v) prop.values.push_back(std::to_string(v));
        } else {
            prop.values = split(body, ',');
        }
        if (prop.defaultValue.empty()) prop.defaultValue = prop.values.front();
        out.push_back(std::move(prop));
    }
    return out;
}

// Defaults for properties inferred from blockstate files (no behavior class).
std::string inferredDefault(const std::string& name, const std::vector<std::string>& values) {
    auto has = [&](std::string_view v) { return std::find(values.begin(), values.end(), v) != values.end(); };
    static const std::vector<std::pair<std::string_view, std::vector<std::string_view>>> preferred = {
        {"facing", {"north", "up"}}, {"half", {"bottom", "lower"}}, {"axis", {"y"}},
        {"type", {"bottom", "single", "normal"}}, {"face", {"wall"}}, {"attachment", {"floor"}},
        {"shape", {"straight", "north_south"}}, {"orientation", {"north_up"}}, {"mode", {"compare", "save"}},
    };
    for (const auto& [prop, wants] : preferred)
        if (prop == name)
            for (auto w : wants)
                if (has(w)) return std::string(w);
    if (has("false")) return "false";
    if (has("none")) return "none";
    return values.front();
}

void sortValues(std::vector<std::string>& values) {
    const bool numeric = std::all_of(values.begin(), values.end(), [](const std::string& v) {
        int x;
        return parseIntStr(v, x);
    });
    if (numeric) {
        std::sort(values.begin(), values.end(), [](const std::string& a, const std::string& b) {
            return std::stoi(a) < std::stoi(b);
        });
    } else if (values.size() == 2 && std::find(values.begin(), values.end(), "true") != values.end() &&
               std::find(values.begin(), values.end(), "false") != values.end()) {
        values = {"false", "true"};
    } else {
        std::sort(values.begin(), values.end());
    }
}

// ------------------------------------------------- emission expressions
//   expr := cond '?' expr ':' expr | sum
//   cond := ['!'] ident
//   sum  := term (('+' | '-') term)*
//   term := atom ('*' atom)*
//   atom := number | ident | ident ':' n0,n1,...   (table lookup by int value)
// Identifiers are property values: booleans give 0/1, integers their value.
class Expr {
public:
    using Get = std::function<std::string_view(std::string_view)>;
    Expr(std::string_view text, const Get& get) : s_(text), get_(get) {}

    int eval() {
        const int v = expr();
        return v;
    }

private:
    int expr() {
        const size_t save = pos_;
        skip();
        bool negate = false;
        if (peek() == '!') {
            negate = true;
            ++pos_;
        }
        const std::string id = ident();
        skip();
        if (!id.empty() && peek() == '?') {
            ++pos_;
            const bool cond = (lookup(id) != 0) != negate;
            const int a = expr();
            skip();
            if (peek() == ':') ++pos_;
            const int b = expr();
            return cond ? a : b;
        }
        pos_ = save;
        return sum();
    }
    int sum() {
        int v = term();
        for (;;) {
            skip();
            if (peek() == '+') {
                ++pos_;
                v += term();
            } else if (peek() == '-') {
                ++pos_;
                v -= term();
            } else {
                return v;
            }
        }
    }
    int term() {
        int v = atom();
        for (;;) {
            skip();
            if (peek() != '*') return v;
            ++pos_;
            v *= atom();
        }
    }
    int atom() {
        skip();
        if (std::isdigit(uint8_t(peek()))) {
            int v = 0;
            while (std::isdigit(uint8_t(peek()))) v = v * 10 + (s_[pos_++] - '0');
            return v;
        }
        const std::string id = ident();
        if (peek() == ':') { // table
            ++pos_;
            std::vector<int> table;
            for (;;) {
                int v = 0;
                while (std::isdigit(uint8_t(peek()))) v = v * 10 + (s_[pos_++] - '0');
                table.push_back(v);
                if (peek() != ',') break;
                ++pos_;
            }
            const int idx = lookup(id);
            return idx >= 0 && idx < int(table.size()) ? table[size_t(idx)] : 0;
        }
        return lookup(id);
    }
    std::string ident() {
        std::string out;
        while (pos_ < s_.size() && (std::isalnum(uint8_t(s_[pos_])) || s_[pos_] == '_')) out += s_[pos_++];
        return out;
    }
    int lookup(const std::string& id) {
        const std::string_view v = get_(id);
        if (v == "true") return 1;
        if (v == "false" || v.empty()) return 0;
        int out = 0;
        parseIntStr(v, out);
        return out;
    }
    char peek() const { return pos_ < s_.size() ? s_[pos_] : '\0'; }
    void skip() {
        while (pos_ < s_.size() && s_[pos_] == ' ') ++pos_;
    }

    std::string_view s_;
    const Get& get_;
    size_t pos_ = 0;
};

struct PropertyRule {
    std::vector<std::string> match;
    Json fields;
};

std::vector<PropertyRule> loadRules(const std::filesystem::path& path, std::string* error) {
    std::vector<PropertyRule> rules;
    std::string err;
    auto j = loadJsonFile(path, &err);
    if (!j || !j->is_array()) {
        if (error) *error = err.empty() ? std::format("{}: expected an array", path.string()) : err;
        return rules;
    }
    for (const auto& r : *j) {
        PropertyRule rule;
        const Json& m = r.value("match", Json::array());
        if (m.is_string()) rule.match.push_back(m.get<std::string>());
        else
            for (const auto& x : m) rule.match.push_back(x.get<std::string>());
        rule.fields = r;
        rules.push_back(std::move(rule));
    }
    return rules;
}

bool ruleMatches(const PropertyRule& r, std::string_view shortName) {
    for (const auto& pattern : r.match)
        if (globMatch(pattern, shortName)) return true;
    return false;
}

// First rule (in file order) that matches and defines `field`.
const Json* firstField(const std::vector<PropertyRule>& rules, std::string_view shortName, const char* field) {
    for (const auto& r : rules) {
        if (!r.fields.contains(field)) continue;
        if (ruleMatches(r, shortName)) return &r.fields[field];
    }
    return nullptr;
}

std::optional<RenderLayer> layerFromName(std::string_view n) {
    if (n == "solid") return RenderLayer::Solid;
    if (n == "cutout_mipped") return RenderLayer::CutoutMipped;
    if (n == "cutout") return RenderLayer::Cutout;
    if (n == "translucent") return RenderLayer::Translucent;
    if (n == "invisible") return RenderLayer::Invisible;
    return std::nullopt;
}

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

int Property::index(std::string_view value) const {
    for (size_t i = 0; i < values.size(); ++i)
        if (values[i] == value) return int(i);
    return -1;
}

uint16_t BlockRegistry::internProperty(const std::string& name, const std::vector<std::string>& values) {
    for (size_t i = 0; i < props_.size(); ++i)
        if (props_[i].name == name && props_[i].values == values) return uint16_t(i);
    Property p;
    p.name = name;
    p.values = values;
    p.boolean = values == std::vector<std::string>{"false", "true"};
    p.integer = std::all_of(values.begin(), values.end(), [](const std::string& v) {
        int x;
        return parseIntStr(v, x);
    });
    props_.push_back(std::move(p));
    return uint16_t(props_.size() - 1);
}

bool BlockRegistry::build(const BlockModels& models, const Atlas& atlas, const std::filesystem::path& dataDir,
                          std::string* error) {
    std::string err;
    const auto classRules = loadRules(dataDir / "blocks" / "classes.json", &err);
    if (!err.empty()) {
        if (error) *error = err;
        return false;
    }
    const auto propRules = loadRules(dataDir / "blocks" / "properties.json", &err);
    if (!err.empty()) {
        if (error) *error = err;
        return false;
    }

    // Air first so it is StateId 0.
    std::vector<const BlockStateDef*> defs;
    for (const auto& d : models.definitions()) defs.push_back(&d);
    std::stable_partition(defs.begin(), defs.end(), [](const BlockStateDef* d) { return d->block == "minecraft:air"; });

    blocks_.clear();
    props_.clear();
    info_.clear();
    choices_.clear();
    choiceStart_.clear();
    blockIndex_.clear();

    struct Pending {
        std::vector<SchemaProp> schema;
    };
    std::vector<Pending> pending;
    std::vector<std::string> problems;

    for (const BlockStateDef* def : defs) {
        Block b;
        b.id = def->block;
        b.index = uint16_t(blocks_.size());
        const std::string_view name = shortId(b.id);

        // Behavior class: first matching rule, else "simple" with an inferred schema.
        Json params;
        for (const auto& r : classRules)
            if (ruleMatches(r, name)) {
                b.className = r.fields.value("class", "simple");
                params = r.fields.value("params", Json::object());
                break;
            }
        std::vector<SchemaProp> schema;
        if (!b.className.empty()) {
            auto it = classSchemas().find(b.className);
            if (it == classSchemas().end()) {
                problems.push_back(std::format("{}: unknown class {}", b.id, b.className));
                b.className.clear();
            } else {
                schema = parseSchema(std::string(it->second), params);
            }
        }
        if (b.className.empty()) {
            b.className = "simple";
            for (auto& [pname, values] : def->mentionedProperties()) {
                SchemaProp p;
                p.name = pname;
                p.values = values;
                sortValues(p.values);
                p.defaultValue = inferredDefault(pname, p.values);
                schema.push_back(std::move(p));
            }
        }
        std::sort(schema.begin(), schema.end(), [](const auto& a, const auto& c) { return a.name < c.name; });

        b.strides.resize(schema.size());
        uint32_t count = 1;
        for (size_t i = schema.size(); i-- > 0;) {
            b.strides[i] = count;
            count *= uint32_t(schema[i].values.size());
        }
        for (const auto& p : schema) b.props.push_back(internProperty(p.name, p.values));
        b.stateCount = count;
        b.first = StateId(info_.size());
        if (info_.size() + count > 65535) {
            if (error) *error = std::format("too many block states (at {})", b.id);
            return false;
        }
        uint32_t defLocal = 0;
        for (size_t i = 0; i < schema.size(); ++i) {
            const int vi = props_[b.props[i]].index(schema[i].defaultValue);
            if (vi < 0) problems.push_back(std::format("{}: bad default {}={}", b.id, schema[i].name, schema[i].defaultValue));
            defLocal += uint32_t(std::max(vi, 0)) * b.strides[i];
        }
        b.defaultState = StateId(b.first + defLocal);
        b.tint = tintRuleFor(b.id);
        info_.resize(info_.size() + count);
        for (uint32_t i = 0; i < count; ++i) info_[b.first + i].block = b.index;
        blockIndex_.emplace(b.id, b.index);
        blocks_.push_back(std::move(b));
        pending.push_back({std::move(schema)});
    }

    // Per-state model, render and light data.
    choiceStart_.assign(info_.size() + 1, 0);
    for (size_t bi = 0; bi < blocks_.size(); ++bi) {
        Block& b = blocks_[bi];
        const BlockStateDef* def = defs[bi];
        const std::string_view name = shortId(b.id);
        const Json* layerJ = firstField(propRules, name, "layer");
        const Json* emissionJ = firstField(propRules, name, "emission");
        const Json* opacityJ = firstField(propRules, name, "opacity");
        const Json* skipSameJ = firstField(propRules, name, "skip_same");
        const Json* replaceableJ = firstField(propRules, name, "replaceable");
        const Json* aoShadeJ = firstField(propRules, name, "ao_shade");
        const bool liquid = b.className == "liquid";
        const bool waterPlant = b.className == "water_plant" || b.className == "tall_water_plant" ||
                                name == "bubble_column";

        for (uint32_t local = 0; local < b.stateCount; ++local) {
            const StateId s = b.stateAt(local);
            StateInfo& si = info_[s];
            auto get = [&](std::string_view prop) -> std::string_view { return value(s, prop); };

            choiceStart_[s] = uint32_t(choices_.size());
            for (const BakedChoice* c : models.choicesFor(*def, get)) choices_.push_back(c);
            const auto mine = std::span(choices_).subspan(choiceStart_[s]);
            if (def->multipart == false && mine.empty())
                problems.push_back(std::format("{}: no variant matches", describe(s)));

            // Geometry-derived data from the first option of each choice.
            bool anyQuad = false, translucent = false, transparent = false;
            std::array<uint16_t, 6> coverage{};
            std::array<uint16_t, 6> shape{}; // coverage by any flush quad, transparent or not
            for (const BakedChoice* c : mine) {
                for (const BakedQuad& q : c->models[0]->quads) {
                    anyQuad = true;
                    const SpriteInfo& sprite = atlas.sprite(q.sprite);
                    translucent |= sprite.translucent;
                    transparent |= sprite.transparent;
                    // Only fully opaque texels occlude (glass and leaves never cull neighbors).
                    if (q.flush && !sprite.translucent && !sprite.transparent)
                        coverage[size_t(q.face)] |= faceCellMask(q.face, q.pos, false);
                    if (q.flush) shape[size_t(q.face)] |= faceCellMask(q.face, q.pos, false);
                }
            }
            si.coverage = coverage;
            if (anyQuad) si.flags |= kHasGeometry;
            if (s == kAirState || name == "cave_air" || name == "void_air") si.flags |= kAir;

            RenderLayer layer = !anyQuad      ? RenderLayer::Invisible
                                : translucent ? RenderLayer::Translucent
                                : transparent ? RenderLayer::Cutout
                                              : RenderLayer::Solid;
            if (layerJ && layerJ->is_string())
                if (auto l = layerFromName(layerJ->get<std::string>())) layer = *l;

            // Fluids.
            if (liquid) {
                const int level = intValue(s, "level");
                si.fluid = {name == "lava" ? FluidKind::Lava : FluidKind::Water, uint8_t(level & 7), level >= 8};
                si.flags |= kCodeRendered;
                layer = name == "lava" ? RenderLayer::Solid : RenderLayer::Translucent;
            } else if (waterPlant || boolValue(s, "waterlogged")) {
                si.fluid = {FluidKind::Water, 0, false};
                si.flags |= kWaterlogged;
            }
            si.layer = layer;
            if (skipSameJ && skipSameJ->is_boolean() && skipSameJ->get<bool>()) si.flags |= kSkipSame;
            {
                bool fullShape = anyQuad;
                for (uint16_t c : shape) fullShape &= c == 0xFFFF;
                if (aoShadeJ && aoShadeJ->is_boolean()) fullShape = aoShadeJ->get<bool>();
                if (fullShape) si.flags |= kAoShade;
            }
            if (replaceableJ) {
                const bool rep = replaceableJ->is_boolean() ? replaceableJ->get<bool>()
                                                            : Expr(replaceableJ->get<std::string>(), get).eval() != 0;
                if (rep) si.flags |= kReplaceable;
            }

            bool opaqueCube = true;
            for (uint16_t c : coverage) opaqueCube &= c == 0xFFFF;
            if (opaqueCube && !liquid) si.flags |= kOpaqueCube;
            for (int d = 0; d < 6; ++d)
                if (coverage[size_t(d)] == 0xFFFF) si.lightBlockingFaces |= uint8_t(1u << d);

            int opacity = (si.flags & kOpaqueCube) ? 15 : 0;
            if (si.fluid.kind != FluidKind::None) opacity = std::max(opacity, 1);
            if (opacityJ) opacity = opacityJ->is_number() ? opacityJ->get<int>()
                                                          : Expr(opacityJ->get<std::string>(), get).eval();
            si.opacity = uint8_t(std::clamp(opacity, 0, 15));

            int emission = 0;
            if (emissionJ) emission = emissionJ->is_number() ? emissionJ->get<int>()
                                                             : Expr(emissionJ->get<std::string>(), get).eval();
            si.emission = uint8_t(std::clamp(emission, 0, 15));
        }
    }
    choiceStart_[info_.size()] = uint32_t(choices_.size());

    std::string tagErrors;
    if (!tags_.load(*this, dataDir / "tags" / "blocks", &tagErrors)) {
        for (size_t start = 0; start < tagErrors.size();) {
            const size_t end = tagErrors.find('\n', start);
            problems.push_back(tagErrors.substr(start, end - start));
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }

    for (const auto& p : problems) log::warn("block registry: {}", p);
    log::info("Block registry: {} blocks, {} states, {} properties, {} tags", blocks_.size(), info_.size(),
              props_.size(), tags_.size());
    if (!problems.empty() && error) *error = problems.front();
    return problems.empty();
}

const Block* BlockRegistry::find(std::string_view id) const {
    auto it = blockIndex_.find(canonicalId(id));
    return it == blockIndex_.end() ? nullptr : &blocks_[it->second];
}

std::span<const BakedChoice* const> BlockRegistry::modelChoices(StateId s) const {
    return std::span(choices_).subspan(choiceStart_[s], choiceStart_[s + 1] - choiceStart_[s]);
}

int BlockRegistry::slotValue(StateId s, size_t slot) const {
    const Block& b = blockOf(s);
    const uint32_t local = s - b.first;
    return int(local / b.strides[slot] % props_[b.props[slot]].values.size());
}

StateId BlockRegistry::withSlot(StateId s, size_t slot, int valueIndex) const {
    const Block& b = blockOf(s);
    const int cur = slotValue(s, slot);
    return StateId(int(s) + (valueIndex - cur) * int(b.strides[slot]));
}

std::string_view BlockRegistry::value(StateId s, std::string_view prop) const {
    const Block& b = blockOf(s);
    for (size_t i = 0; i < b.props.size(); ++i) {
        const Property& p = props_[b.props[i]];
        if (p.name == prop) return p.values[size_t(slotValue(s, i))];
    }
    return {};
}

int BlockRegistry::intValue(StateId s, std::string_view prop, int fallback) const {
    const std::string_view v = value(s, prop);
    int out = fallback;
    if (!v.empty()) parseIntStr(v, out);
    return out;
}

std::optional<StateId> BlockRegistry::with(StateId s, std::string_view prop, std::string_view val) const {
    const Block& b = blockOf(s);
    for (size_t i = 0; i < b.props.size(); ++i) {
        const Property& p = props_[b.props[i]];
        if (p.name != prop) continue;
        const int vi = p.index(val);
        if (vi < 0) return std::nullopt;
        return withSlot(s, i, vi);
    }
    return std::nullopt;
}

std::string BlockRegistry::describe(StateId s) const {
    const Block& b = blockOf(s);
    std::string out = b.id;
    if (!b.props.empty()) {
        out += '[';
        for (size_t i = 0; i < b.props.size(); ++i) {
            if (i) out += ',';
            const Property& p = props_[b.props[i]];
            out += p.name + "=" + p.values[size_t(slotValue(s, i))];
        }
        out += ']';
    }
    return out;
}

std::optional<StateId> BlockRegistry::parse(std::string_view text, std::string* error) const {
    const size_t bracket = text.find('[');
    const std::string_view id = text.substr(0, bracket);
    const Block* b = find(id);
    if (!b) {
        if (error) *error = std::format("unknown block '{}'", id);
        return std::nullopt;
    }
    StateId s = b->defaultState;
    if (bracket == std::string_view::npos) return s;
    if (text.back() != ']') {
        if (error) *error = "missing ']'";
        return std::nullopt;
    }
    for (const auto& kv : split(text.substr(bracket + 1, text.size() - bracket - 2), ',')) {
        const size_t eq = kv.find('=');
        if (eq == std::string::npos) {
            if (error) *error = std::format("expected property=value, got '{}'", kv);
            return std::nullopt;
        }
        auto next = with(s, kv.substr(0, eq), kv.substr(eq + 1));
        if (!next) {
            if (error) *error = std::format("{} has no property value {}", b->id, kv);
            return std::nullopt;
        }
        s = *next;
    }
    return s;
}

StateId BlockRegistry::defaultState(std::string_view blockId) const {
    const Block* b = find(blockId);
    return b ? b->defaultState : kAirState;
}

} // namespace mc
