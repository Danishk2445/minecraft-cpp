#include "resources/TextureMeta.h"

namespace mc {

TextureMeta parseTextureMeta(const Json& j) {
    TextureMeta meta;
    if (!j.is_object()) return meta;

    if (auto it = j.find("animation"); it != j.end() && it->is_object()) {
        AnimationMeta a;
        a.frameTime = std::max(1, jsonGet<int>(*it, "frametime", 1));
        a.interpolate = jsonGet<bool>(*it, "interpolate", false);
        a.width = jsonGet<int>(*it, "width", -1);
        a.height = jsonGet<int>(*it, "height", -1);
        if (auto f = it->find("frames"); f != it->end() && f->is_array()) {
            for (const auto& e : *f) {
                if (e.is_number_integer()) {
                    a.frames.push_back({e.get<int>(), a.frameTime});
                } else if (e.is_object()) {
                    a.frames.push_back({jsonGet<int>(e, "index", 0), std::max(1, jsonGet<int>(e, "time", a.frameTime))});
                }
            }
        }
        meta.animation = a;
    }

    if (auto it = j.find("texture"); it != j.end() && it->is_object()) {
        meta.blur = jsonGet<bool>(*it, "blur", false);
        meta.clamp = jsonGet<bool>(*it, "clamp", false);
    }

    if (auto it = j.find("gui"); it != j.end() && it->is_object()) {
        if (auto s = it->find("scaling"); s != it->end() && s->is_object()) {
            GuiScaling g;
            const std::string type = jsonGet<std::string>(*s, "type", "stretch");
            g.type = type == "nine_slice" ? GuiScaling::Type::NineSlice
                     : type == "tile"     ? GuiScaling::Type::Tile
                                          : GuiScaling::Type::Stretch;
            g.width = jsonGet<int>(*s, "width", 0);
            g.height = jsonGet<int>(*s, "height", 0);
            g.stretchInner = jsonGet<bool>(*s, "stretch_inner", false);
            if (auto b = s->find("border"); b != s->end()) {
                if (b->is_number()) {
                    g.left = g.top = g.right = g.bottom = b->get<int>();
                } else if (b->is_object()) {
                    g.left = jsonGet<int>(*b, "left", 0);
                    g.top = jsonGet<int>(*b, "top", 0);
                    g.right = jsonGet<int>(*b, "right", 0);
                    g.bottom = jsonGet<int>(*b, "bottom", 0);
                }
            }
            meta.gui = g;
        }
    }
    return meta;
}

} // namespace mc
