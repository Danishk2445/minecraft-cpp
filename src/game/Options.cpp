#include "game/Options.h"

#include <algorithm>

#include "core/Fs.h"
#include "core/Json.h"

namespace mc {
namespace {

template <class E>
E enumFrom(const Json& j, const char* key, E fallback, int count) {
    const int v = j.value(key, int(fallback));
    return v >= 0 && v < count ? E(v) : fallback;
}

} // namespace

std::string Options::toJson() const {
    Json keyNames = Json::object();
    for (size_t i = 0; i < kKeyActionCount; ++i) keyNames[keyMappingInfo(KeyAction(i)).name] = keys.keys[i].name();
    const Json j = {
        {"version", 1},
        {"graphics", int(graphics)},
        {"fov", fov},
        {"fovEffectScale", fovEffectScale},
        {"renderDistance", renderDistance},
        {"guiScale", guiScale},
        {"maxFps", maxFps},
        {"vsync", vsync},
        {"fullscreen", fullscreen},
        {"smoothLighting", smoothLighting},
        {"gamma", gamma},
        {"entityShadows", entityShadows},
        {"viewBobbing", viewBobbing},
        {"clouds", int(clouds)},
        {"particles", int(particles)},
        {"menuBlur", menuBlur},
        {"biomeBlend", biomeBlend},
        {"mipmapLevels", mipmapLevels},
        {"entityDistance", entityDistance},
        {"attackIndicator", int(attackIndicator)},
        {"mouseSensitivity", mouseSensitivity},
        {"invertMouse", invertMouse},
        {"chatVisibility", int(chatVisibility)},
        {"chatOpacity", chatOpacity},
        {"textBackgroundOpacity", textBackgroundOpacity},
        {"chatScale", chatScale},
        {"keys", keyNames},
    };
    return j.dump(2) + "\n";
}

Options Options::fromJson(std::string_view text, std::string* error) {
    Options o;
    Json j = Json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        if (error) *error = "options: not a JSON object";
        return o;
    }
    try {
        o.graphics = enumFrom(j, "graphics", o.graphics, 3);
        o.fov = std::clamp(j.value("fov", o.fov), 30, 110);
        o.fovEffectScale = std::clamp(j.value("fovEffectScale", o.fovEffectScale), 0.0f, 1.0f);
        o.renderDistance = std::clamp(j.value("renderDistance", o.renderDistance), 2, 32);
        o.guiScale = std::max(0, j.value("guiScale", o.guiScale));
        o.maxFps = std::clamp(j.value("maxFps", o.maxFps), 10, 260);
        o.vsync = j.value("vsync", o.vsync);
        o.fullscreen = j.value("fullscreen", o.fullscreen);
        o.smoothLighting = j.value("smoothLighting", o.smoothLighting);
        o.gamma = std::clamp(j.value("gamma", o.gamma), 0.0f, 1.0f);
        o.entityShadows = j.value("entityShadows", o.entityShadows);
        o.viewBobbing = j.value("viewBobbing", o.viewBobbing);
        o.clouds = enumFrom(j, "clouds", o.clouds, 3);
        o.particles = enumFrom(j, "particles", o.particles, 3);
        o.menuBlur = std::clamp(j.value("menuBlur", o.menuBlur), 0, 10);
        o.biomeBlend = std::clamp(j.value("biomeBlend", o.biomeBlend), 0, 7);
        o.mipmapLevels = std::clamp(j.value("mipmapLevels", o.mipmapLevels), 0, 4);
        o.entityDistance = std::clamp(j.value("entityDistance", o.entityDistance), 0.5f, 5.0f);
        o.attackIndicator = enumFrom(j, "attackIndicator", o.attackIndicator, 3);
        o.mouseSensitivity = std::clamp(j.value("mouseSensitivity", o.mouseSensitivity), 0.0f, 1.0f);
        o.invertMouse = j.value("invertMouse", o.invertMouse);
        o.chatVisibility = enumFrom(j, "chatVisibility", o.chatVisibility, 3);
        o.chatOpacity = std::clamp(j.value("chatOpacity", o.chatOpacity), 0.1f, 1.0f);
        o.textBackgroundOpacity = std::clamp(j.value("textBackgroundOpacity", o.textBackgroundOpacity), 0.0f, 1.0f);
        o.chatScale = std::clamp(j.value("chatScale", o.chatScale), 0.0f, 1.0f);
        // Vanilla names; unknown actions and keys keep the defaults.
        if (const auto it = j.find("keys"); it != j.end() && it->is_object())
            for (size_t i = 0; i < kKeyActionCount; ++i)
                if (const auto k = it->find(keyMappingInfo(KeyAction(i)).name); k != it->end() && k->is_string())
                    if (const auto key = InputKey::parse(k->get<std::string>())) o.keys.keys[i] = *key;
    } catch (const Json::exception& e) {
        if (error) *error = std::string("options: ") + e.what();
    }
    return o;
}

Options Options::load(const std::filesystem::path& file, std::string* error) {
    const auto text = fs::readText(file);
    return text ? fromJson(*text, error) : Options{};
}

bool Options::save(const std::filesystem::path& file, std::string* error) const {
    if (fs::writeTextAtomic(file, toJson())) return true;
    if (error) *error = "options: could not write " + file.string();
    return false;
}

} // namespace mc
