#include "render/Gallery.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <set>

#include <glm/gtc/matrix_transform.hpp>

#include "core/Fs.h"
#include "core/Json.h"
#include "core/Log.h"
#include "gfx/GL.h"
#include "world/mesh/ChunkMesher.h"

namespace mc {
namespace {

uint32_t packColor(const glm::vec3& c, float a = 1.0f) {
    auto ch = [](float v) { return uint32_t(std::clamp(int(v * 255.0f + 0.5f), 0, 255)); };
    return ch(c.r) | (ch(c.g) << 8) | (ch(c.b) << 16) | (ch(a) << 24);
}

int propInt(const VisualState& s, std::string_view name) {
    const std::string_view v = s.prop(name);
    int out = 0;
    std::from_chars(v.data(), v.data() + v.size(), out);
    return out;
}

std::string sanitize(std::string_view s) {
    std::string out(shortId(s));
    for (char& c : out)
        if (c == '[' || c == ']' || c == ',' || c == '=' || c == ':' || c == '/') c = '_';
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out;
}

void setupVertexArray(unsigned vao) {
    glEnableVertexArrayAttrib(vao, 0);
    glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(MeshVertex, pos));
    glVertexArrayAttribBinding(vao, 0, 0);
    glEnableVertexArrayAttrib(vao, 1);
    glVertexArrayAttribFormat(vao, 1, 2, GL_FLOAT, GL_FALSE, offsetof(MeshVertex, uv));
    glVertexArrayAttribBinding(vao, 1, 0);
    glEnableVertexArrayAttrib(vao, 2);
    glVertexArrayAttribFormat(vao, 2, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(MeshVertex, color));
    glVertexArrayAttribBinding(vao, 2, 0);
}

void drawLayers(const Shader& shader, const GpuMesh& opaque, const GpuMesh& translucent) {
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    shader.set("uAlphaCutoff", 0.1f);
    opaque.draw();
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    shader.set("uAlphaCutoff", 0.004f);
    translucent.draw();
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

} // namespace

std::string VisualState::describe() const {
    std::string s = def->block;
    if (!props.empty()) {
        s += '[';
        for (size_t i = 0; i < props.size(); ++i) {
            if (i) s += ',';
            s += props[i].first + "=" + props[i].second;
        }
        s += ']';
    }
    return s;
}

std::string_view VisualState::prop(std::string_view name) const {
    for (const auto& [k, v] : props)
        if (k == name) return v;
    return {};
}

bool VisualState::hasGeometry() const {
    for (const BakedChoice* c : choices)
        for (const BakedModel* m : c->models)
            if (!m->quads.empty()) return true;
    return false;
}

std::vector<VisualState> enumerateVisualStates(const BlockModels& models) {
    std::vector<VisualState> out;
    for (const auto& def : models.definitions()) {
        if (!def.multipart) {
            for (const auto& v : def.variants) {
                VisualState s;
                s.def = &def;
                s.props = v.props;
                s.choices = {&models.baked(v.choice)};
                out.push_back(std::move(s));
            }
            continue;
        }
        auto props = def.mentionedProperties();
        std::sort(props.begin(), props.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<size_t> idx(props.size(), 0);
        for (;;) {
            VisualState s;
            s.def = &def;
            for (size_t i = 0; i < props.size(); ++i) s.props.emplace_back(props[i].first, props[i].second[idx[i]]);
            s.choices = models.choicesFor(def, [&s](std::string_view p) { return s.prop(p); });
            out.push_back(std::move(s));
            size_t k = 0;
            while (k < idx.size() && ++idx[k] == props[k].second.size()) idx[k++] = 0;
            if (k == idx.size()) break;
        }
    }
    return out;
}

void appendStateQuads(const VisualState& state, const BlockPos& seedPos, const glm::mat4& xform,
                      const ClientAssets& assets, SimpleMesh& opaque, SimpleMesh& translucent) {
    std::vector<const BakedModel*> models;
    BlockModels::collect(state.choices, seedPos, models);
    const TintRule rule = tintRuleFor(state.def->block);
    const BiomeColors biome = assets.defaultBiomeColors();
    const int power = propInt(state, "power"), age = propInt(state, "age");
    const Atlas& atlas = assets.blockAtlas();
    for (const BakedModel* m : models) {
        for (const BakedQuad& q : m->quads) {
            SimpleMesh& mesh = atlas.sprite(q.sprite).translucent ? translucent : opaque;
            const uint32_t tint = resolveTint(rule, q.tint, biome, power, age);
            const float shade = q.shade ? blockFaceShade(q.face) : 1.0f;
            const glm::vec3 color =
                glm::vec3(float((tint >> 16) & 255), float((tint >> 8) & 255), float(tint & 255)) / 255.0f * shade;
            const uint32_t packed = packColor(color);
            const uint32_t base = uint32_t(mesh.vertices.size());
            for (int i = 0; i < 4; ++i)
                mesh.vertices.push_back({glm::vec3(xform * glm::vec4(q.pos[size_t(i)], 1.0f)), q.uv[size_t(i)], packed});
            mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base + 2, base + 3, base});
        }
    }
}

GpuMesh::~GpuMesh() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
}

void GpuMesh::upload(const SimpleMesh& mesh) {
    indexCount_ = mesh.indices.size();
    if (indexCount_ == 0) return;
    vbo_.create(mesh.vertices.size() * sizeof(MeshVertex), mesh.vertices.data());
    ibo_.create(mesh.indices.size() * sizeof(uint32_t), mesh.indices.data());
    if (!vao_) {
        glCreateVertexArrays(1, &vao_);
        setupVertexArray(vao_);
    }
    glVertexArrayVertexBuffer(vao_, 0, vbo_.id(), 0, sizeof(MeshVertex));
    glVertexArrayElementBuffer(vao_, ibo_.id());
}

void GpuMesh::draw() const {
    if (indexCount_ == 0) return;
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, GLsizei(indexCount_), GL_UNSIGNED_INT, nullptr);
}

bool Gallery::init(const ClientAssets& assets) {
    assets_ = &assets;
    if (!shader_.load("gallery.vert", "gallery.frag")) return false;
    const auto states = enumerateVisualStates(assets.blockModels());
    SimpleMesh opaque, translucent;
    constexpr int kPerRow = 96;
    for (size_t i = 0; i < states.size(); ++i) {
        const BlockPos p(int(i % kPerRow) * 2, 0, int(i / kPerRow) * 2);
        const glm::mat4 xf = glm::translate(glm::mat4(1.0f), glm::vec3(p.vec()));
        appendStateQuads(states[i], p, xf, assets, opaque, translucent);
    }
    opaque_.upload(opaque);
    translucent_.upload(translucent);
    stateCount_ = states.size();
    start_ = {-8.0, 14.0, -8.0};
    log::info("Gallery: {} block states, {} quads", stateCount_, (opaque.indices.size() + translucent.indices.size()) / 6);
    return true;
}

void Gallery::render(const Camera& camera, glm::ivec2 viewport) {
    glViewport(0, 0, viewport.x, viewport.y);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    shader_.use();
    const float aspect = float(viewport.x) / float(std::max(viewport.y, 1));
    shader_.set("uViewProj", camera.projection(aspect) * camera.view());
    shader_.set("uCameraPos", glm::vec3(camera.position));
    assets_->blockAtlasTexture().bind(0);
    drawLayers(shader_, opaque_, translucent_);
}

int dumpModelSheets(const ClientAssets& assets, const std::filesystem::path& dir,
                    const std::filesystem::path& goldenList) {
    Shader shader;
    if (!shader.load("gallery.vert", "gallery.frag")) return 1;
    const auto states = enumerateVisualStates(assets.blockModels());

    // Golden list format: one state per line; "empty-ok <state>" marks a state that
    // legitimately renders nothing from the GUI angle (e.g. fire's downward-facing top).
    std::set<std::string> goldens, emptyOk;
    if (!goldenList.empty()) {
        std::ifstream in(goldenList);
        for (std::string line; std::getline(in, line);) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            if (line.starts_with("empty-ok ")) emptyOk.insert(line.substr(9));
            else goldens.insert(line);
        }
    }
    {
        std::string index;
        for (const auto& st : states) index += st.describe() + "\n";
        fs::writeTextAtomic(dir / "states.txt", index);
    }
    std::error_code ec;
    std::filesystem::create_directories(dir / "tiles", ec);

    constexpr int kTile = 32, kPerRow = 64, kSheet = kTile * kPerRow, kPerSheet = kPerRow * kPerRow;
    Texture color, depth;
    color.create(kSheet, kSheet, 1);
    depth.createDepth(kSheet, kSheet);
    unsigned fbo = 0;
    glCreateFramebuffers(1, &fbo);
    glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, color.id(), 0);
    glNamedFramebufferTexture(fbo, GL_DEPTH_STENCIL_ATTACHMENT, depth.id(), 0);
    if (glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        log::error("model sheets: framebuffer incomplete");
        glDeleteFramebuffers(1, &fbo);
        return 1;
    }

    Json report = {{"states", states.size()}, {"tile", kTile}, {"failures", Json::array()}};
    int failures = 0, empty = 0;
    std::vector<uint8_t> pixels(size_t(kSheet) * kSheet * 4);
    const glm::mat4 gui = glm::rotate(glm::mat4(1.0f), glm::radians(30.0f), glm::vec3(1, 0, 0)) *
                          glm::rotate(glm::mat4(1.0f), glm::radians(225.0f), glm::vec3(0, 1, 0)) *
                          glm::scale(glm::mat4(1.0f), glm::vec3(0.625f)) *
                          glm::translate(glm::mat4(1.0f), glm::vec3(-0.5f));

    for (size_t first = 0, sheet = 0; first < states.size(); first += kPerSheet, ++sheet) {
        const size_t count = std::min<size_t>(kPerSheet, states.size() - first);
        SimpleMesh opaque, translucent;
        for (size_t i = 0; i < count; ++i) {
            const int col = int(i % kPerRow), row = int(i / kPerRow);
            const glm::mat4 xf =
                glm::translate(glm::mat4(1.0f), glm::vec3(col * kTile + kTile / 2, row * kTile + kTile / 2, 0)) *
                glm::scale(glm::mat4(1.0f), glm::vec3(kTile, -kTile, kTile)) * gui;
            appendStateQuads(states[first + i], BlockPos(0, 0, 0), xf, assets, opaque, translucent);
        }
        GpuMesh gpuOpaque, gpuTranslucent;
        gpuOpaque.upload(opaque);
        gpuTranslucent.upload(translucent);

        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, kSheet, kSheet);
        const float clear[4] = {0, 0, 0, 0};
        glClearNamedFramebufferfv(fbo, GL_COLOR, 0, clear);
        glClearNamedFramebufferfi(fbo, GL_DEPTH_STENCIL, 0, 1.0f, 0);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_CULL_FACE);
        glFrontFace(GL_CCW); // the two y flips (tile space, ortho) cancel out
        shader.use();
        shader.set("uViewProj", glm::ortho(0.0f, float(kSheet), float(kSheet), 0.0f, -1000.0f, 1000.0f));
        shader.set("uCameraPos", glm::vec3(0.0f));
        assets.blockAtlasTexture().bind(0);
        drawLayers(shader, gpuOpaque, gpuTranslucent);

        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, kSheet, kSheet, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        Image img(kSheet, kSheet);
        for (int y = 0; y < kSheet; ++y)
            std::memcpy(img.px(0, y), pixels.data() + size_t(kSheet - 1 - y) * kSheet * 4, size_t(kSheet) * 4);

        for (size_t i = 0; i < count; ++i) {
            const VisualState& st = states[first + i];
            const int x0 = int(i % kPerRow) * kTile, y0 = int(i / kPerRow) * kTile;
            int opaquePx = 0, magentaPx = 0;
            for (int y = y0; y < y0 + kTile; ++y)
                for (int x = x0; x < x0 + kTile; ++x) {
                    const uint8_t* p = img.px(x, y);
                    if (p[3] == 0) continue;
                    ++opaquePx;
                    if (p[0] >= 240 && p[1] <= 16 && p[2] >= 240) ++magentaPx;
                }
            const bool geometry = st.hasGeometry();
            if (!geometry) ++empty;
            std::string problem;
            if (magentaPx > 0) problem = std::format("{} missing-texture pixels", magentaPx);
            else if (geometry && opaquePx < 4 && !emptyOk.contains(st.describe())) problem = "renders empty";
            if (!problem.empty()) {
                ++failures;
                if (failures <= 50) log::warn("model sheet: {}: {}", st.describe(), problem);
                report["failures"].push_back({{"state", st.describe()}, {"problem", problem}, {"sheet", sheet}});
            }
            if (goldens.contains(st.describe())) {
                img.region(x0, y0, kTile, kTile).savePng(dir / "tiles" / (sanitize(st.describe()) + ".png"));
                goldens.erase(st.describe());
            }
        }
        img.savePng(dir / std::format("sheet_{}.png", sheet));
    }
    glDeleteFramebuffers(1, &fbo);
    for (const auto& g : goldens) log::warn("golden tile state not found: {}", g);

    report["empty_by_design"] = empty;
    report["failure_count"] = failures;
    fs::writeTextAtomic(dir / "report.json", report.dump(2));
    log::info("Model sheets: {} states, {} without geometry, {} failures -> {}", states.size(), empty, failures,
              dir.string());
    return failures + int(goldens.size());
}

} // namespace mc
