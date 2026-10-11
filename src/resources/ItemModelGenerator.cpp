#include "resources/ItemModelGenerator.h"

namespace mc {
namespace {

// Vanilla SpanFacing: which neighbor of an opaque texel is transparent.
enum class Facing { Up, Down, Left, Right };

struct Span {
    Facing facing;
    int min, max; // along the row (up/down) or column (left/right)
    int anchor;   // the row (up/down) or column (left/right)
};

bool horizontal(Facing f) { return f == Facing::Up || f == Facing::Down; }

// Vanilla createOrExpandSpan: one span per facing and row/column; it grows to
// cover every edge texel there, even across gaps.
void addToSpan(std::vector<Span>& spans, Facing facing, int x, int y) {
    const int anchor = horizontal(facing) ? y : x, pos = horizontal(facing) ? x : y;
    for (Span& s : spans)
        if (s.facing == facing && s.anchor == anchor) {
            if (pos < s.min) s.min = pos;
            else if (pos > s.max) s.max = pos;
            return;
        }
    spans.push_back({facing, pos, pos, anchor});
}

} // namespace

std::vector<ElementDef> generateLayerElements(int layer, const std::string& textureVar,
                                              const std::vector<const Image*>& frames) {
    std::vector<ElementDef> out;
    auto face = [&](float u0, float v0, float u1, float v1) {
        FaceDef f;
        f.uv = {u0, v0, u1, v1};
        f.hasUv = true;
        f.texture = textureVar;
        f.tint = layer;
        return f;
    };
    // The whole layer: the front faces south, the back (mirrored) north.
    ElementDef front;
    front.from = {0.0f, 0.0f, 7.5f};
    front.to = {16.0f, 16.0f, 8.5f};
    front.faces[size_t(Direction::South)] = face(0, 0, 16, 16);
    front.faces[size_t(Direction::North)] = face(16, 0, 0, 16);
    out.push_back(front);
    if (frames.empty() || !frames[0] || frames[0]->empty()) return out;

    const int w = frames[0]->width, h = frames[0]->height;
    std::vector<Span> spans;
    for (const Image* frame : frames) {
        if (!frame || frame->width != w || frame->height != h) continue;
        auto transparent = [&](int x, int y) { return x < 0 || y < 0 || x >= w || y >= h || frame->alpha(x, y) == 0; };
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                if (transparent(x, y)) continue;
                if (transparent(x, y - 1)) addToSpan(spans, Facing::Up, x, y);
                if (transparent(x, y + 1)) addToSpan(spans, Facing::Down, x, y);
                if (transparent(x - 1, y)) addToSpan(spans, Facing::Left, x, y);
                if (transparent(x + 1, y)) addToSpan(spans, Facing::Right, x, y);
            }
    }
    // Texels to model pixels; the image's rows run down, model y runs up.
    const float px = 16.0f / float(w), py = 16.0f / float(h);
    for (const Span& s : spans) {
        ElementDef e;
        const float lo = float(s.min), hi = float(s.max + 1), a = float(s.anchor);
        switch (s.facing) {
        case Facing::Up: // the top edge of row `anchor`, facing up
            e.from = {lo * px, 16.0f - a * py, 7.5f};
            e.to = {hi * px, 16.0f - a * py, 8.5f};
            e.faces[size_t(Direction::Up)] = face(lo * px, a * py, hi * px, (a + 1) * py);
            break;
        case Facing::Down: // the bottom edge, facing down
            e.from = {lo * px, 16.0f - (a + 1) * py, 7.5f};
            e.to = {hi * px, 16.0f - (a + 1) * py, 8.5f};
            e.faces[size_t(Direction::Down)] = face(lo * px, a * py, hi * px, (a + 1) * py);
            break;
        case Facing::Left: // the left edge of column `anchor`, facing west
            e.from = {a * px, 16.0f - hi * py, 7.5f};
            e.to = {a * px, 16.0f - lo * py, 8.5f};
            e.faces[size_t(Direction::West)] = face((a + 1) * px, lo * py, a * px, hi * py);
            break;
        case Facing::Right: // the right edge, facing east
            e.from = {(a + 1) * px, 16.0f - hi * py, 7.5f};
            e.to = {(a + 1) * px, 16.0f - lo * py, 8.5f};
            e.faces[size_t(Direction::East)] = face(a * px, lo * py, (a + 1) * px, hi * py);
            break;
        }
        out.push_back(e);
    }
    return out;
}

} // namespace mc
