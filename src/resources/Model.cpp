#include "resources/Model.h"

#include <format>

namespace mc {
namespace {

constexpr std::array<std::string_view, size_t(DisplayContext::Count)> kDisplayNames = {
    "thirdperson_righthand", "thirdperson_lefthand", "firstperson_righthand", "firstperson_lefthand",
    "head",                  "gui",                  "ground",                "fixed"};

bool readVec3(const Json& j, glm::vec3& out) {
    if (!j.is_array() || j.size() != 3) return false;
    for (int i = 0; i < 3; ++i) {
        if (!j[size_t(i)].is_number()) return false;
        out[i] = j[size_t(i)].get<float>();
    }
    return true;
}

std::string stripHash(std::string_view s) {
    if (s.starts_with('#')) s.remove_prefix(1);
    return std::string(s);
}

std::optional<ElementDef> parseElement(const Json& e, std::string* error) {
    ElementDef el;
    if (!readVec3(e.value("from", Json()), el.from) || !readVec3(e.value("to", Json()), el.to)) {
        if (error) *error = "element without valid from/to";
        return std::nullopt;
    }
    if (auto r = e.find("rotation"); r != e.end() && r->is_object()) {
        el.hasRotation = true;
        readVec3(r->value("origin", Json::array({8, 8, 8})), el.rotationOrigin);
        const std::string axis = jsonGet<std::string>(*r, "axis", "y");
        el.rotationAxis = axis == "x" ? Axis::X : axis == "z" ? Axis::Z : Axis::Y;
        el.rotationAngle = jsonGet<float>(*r, "angle", 0.0f);
        el.rescale = jsonGet<bool>(*r, "rescale", false);
    }
    el.shade = jsonGet<bool>(e, "shade", true);
    el.lightEmission = std::clamp(jsonGet<int>(e, "light_emission", 0), 0, 15);
    if (auto faces = e.find("faces"); faces != e.end() && faces->is_object()) {
        for (auto it = faces->begin(); it != faces->end(); ++it) {
            const auto dir = directionFromName(it.key());
            if (!dir || !it->is_object()) continue;
            FaceDef f;
            if (auto uv = it->find("uv"); uv != it->end() && uv->is_array() && uv->size() == 4) {
                for (int i = 0; i < 4; ++i) f.uv[size_t(i)] = (*uv)[size_t(i)].get<float>();
                f.hasUv = true;
            }
            f.texture = stripHash(jsonGet<std::string>(*it, "texture", ""));
            if (auto c = it->find("cullface"); c != it->end() && c->is_string()) {
                if (auto cd = directionFromName(c->get<std::string>())) f.cull = int(*cd);
            }
            f.rotation = ((jsonGet<int>(*it, "rotation", 0) % 360) + 360) % 360;
            f.tint = jsonGet<int>(*it, "tintindex", -1);
            el.faces[size_t(*dir)] = f;
        }
    }
    return el;
}

} // namespace

std::optional<DisplayContext> displayContextFromName(std::string_view name) {
    for (size_t i = 0; i < kDisplayNames.size(); ++i)
        if (kDisplayNames[i] == name) return DisplayContext(i);
    return std::nullopt;
}

std::optional<ModelDef> ModelDef::parse(std::string name, const Json& j, std::string* error) {
    if (!j.is_object()) {
        if (error) *error = std::format("{}: model is not an object", name);
        return std::nullopt;
    }
    ModelDef m;
    m.name = std::move(name);
    if (auto p = j.find("parent"); p != j.end() && p->is_string()) m.parent = canonicalId(p->get<std::string>());
    if (auto t = j.find("textures"); t != j.end() && t->is_object()) {
        for (auto it = t->begin(); it != t->end(); ++it) {
            if (it->is_string()) m.textures.emplace_back(it.key(), it->get<std::string>());
            else if (it->is_object() && it->contains("sprite"))
                m.textures.emplace_back(it.key(), (*it)["sprite"].get<std::string>());
        }
    }
    if (auto e = j.find("elements"); e != j.end() && e->is_array()) {
        std::vector<ElementDef> elements;
        for (const auto& el : *e) {
            std::string err;
            auto parsed = parseElement(el, &err);
            if (!parsed) {
                if (error) *error = std::format("{}: {}", m.name, err);
                return std::nullopt;
            }
            elements.push_back(std::move(*parsed));
        }
        m.elements = std::move(elements);
    }
    if (auto ao = j.find("ambientocclusion"); ao != j.end() && ao->is_boolean()) m.ambientOcclusion = ao->get<bool>();
    if (auto gl = j.find("gui_light"); gl != j.end() && gl->is_string()) m.guiLightFront = gl->get<std::string>() == "front";
    if (auto d = j.find("display"); d != j.end() && d->is_object()) {
        for (auto it = d->begin(); it != d->end(); ++it) {
            const auto ctx = displayContextFromName(it.key());
            if (!ctx || !it->is_object()) continue;
            ItemTransform t;
            readVec3(it->value("rotation", Json::array({0, 0, 0})), t.rotation);
            readVec3(it->value("translation", Json::array({0, 0, 0})), t.translation);
            readVec3(it->value("scale", Json::array({1, 1, 1})), t.scale);
            m.display[size_t(*ctx)] = t;
        }
    }
    return m;
}

std::string ResolvedModel::resolveTexture(std::string_view variable) const {
    std::string name(variable.starts_with('#') ? variable.substr(1) : variable);
    for (int depth = 0; depth < 32; ++depth) {
        auto it = textures.find(name);
        if (it == textures.end()) return {};
        const std::string& value = it->second;
        if (!value.starts_with('#')) return canonicalId(value);
        name = value.substr(1);
    }
    return {}; // reference cycle
}

ModelStore::ModelStore(const ResourcePack& pack) : pack_(pack) {}

const ModelDef* ModelStore::get(const std::string& id) {
    if (auto it = models_.find(id); it != models_.end()) return it->second.get();
    std::unique_ptr<ModelDef> model;
    if (id == "minecraft:builtin/generated" || id == "minecraft:builtin/entity") {
        model = std::make_unique<ModelDef>();
        model->name = id;
    } else {
        const auto loc = ResourceLocation::parse(id);
        std::string err;
        if (auto j = pack_.json(pack_.resource(loc, "models", ".json"), &err)) {
            if (auto parsed = ModelDef::parse(id, *j, &err)) model = std::make_unique<ModelDef>(std::move(*parsed));
        }
        if (!model) errors_.push_back(err.empty() ? std::format("model {} not found", id) : err);
    }
    const ModelDef* raw = model.get();
    models_.emplace(id, std::move(model));
    return raw;
}

std::optional<ResolvedModel> ModelStore::resolve(const std::string& id) {
    std::vector<const ModelDef*> chain; // leaf first
    std::string cur = canonicalId(id);
    while (!cur.empty()) {
        const ModelDef* m = get(cur);
        if (!m) return std::nullopt;
        for (const ModelDef* seen : chain)
            if (seen == m) {
                errors_.push_back(std::format("model {}: parent cycle at {}", id, cur));
                return std::nullopt;
            }
        chain.push_back(m);
        cur = m->parent;
    }

    ResolvedModel r;
    r.name = canonicalId(id);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const ModelDef& m = **it;
        for (const auto& [k, v] : m.textures) r.textures[k] = v; // child (later) wins
        if (m.name == "minecraft:builtin/generated") r.builtinGenerated = true;
    }
    bool aoSet = false, guiSet = false;
    for (const ModelDef* m : chain) {
        if (!r.elements && m->elements) r.elements = &*m->elements;
        if (!aoSet && m->ambientOcclusion) {
            r.ambientOcclusion = *m->ambientOcclusion;
            aoSet = true;
        }
        if (!guiSet && m->guiLightFront) {
            r.guiLightFront = *m->guiLightFront;
            guiSet = true;
        }
        for (size_t c = 0; c < r.display.size(); ++c)
            if (!r.hasDisplay[c] && m->display[c]) {
                r.display[c] = *m->display[c];
                r.hasDisplay[c] = true;
            }
    }
    return r;
}

} // namespace mc
