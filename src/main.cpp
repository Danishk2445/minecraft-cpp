#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "blocks.h"
#include "frustum.h"
#include "mesher.h"
#include "mobs.h"
#include "player.h"
#include "shader.h"
#include "texture.h"
#include "world.h"

namespace fs = std::filesystem;

// ---------------------------------------------------------------- shaders

static const char* CHUNK_VS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in float aLayer;
layout(location=3) in float aShade;
layout(location=4) in float aSky;
layout(location=5) in float aBlk;
uniform mat4 uVP;
uniform vec3 uOrigin;
uniform vec3 uCam;
out vec2 vUV;
flat out float vLayer;
out float vShade;
out float vSky;
out float vBlk;
out float vDist;
void main(){
    vec3 wp = aPos + uOrigin;
    gl_Position = uVP * vec4(wp, 1.0);
    vUV = aUV; vLayer = aLayer; vShade = aShade; vSky = aSky; vBlk = aBlk;
    vDist = distance(wp, uCam);
})";

static const char* CHUNK_FS = R"(#version 330 core
in vec2 vUV;
flat in float vLayer;
in float vShade;
in float vSky;
in float vBlk;
in float vDist;
uniform sampler2DArray uTex;
uniform vec3 uFog;
uniform float uFogStart, uFogEnd;
uniform float uSun;     // daylight factor 0..1
uniform int uPass;      // 0 = opaque/cutout, 1 = water
uniform float uAlpha;
out vec4 o;
void main(){
    vec4 c = texture(uTex, vec3(vUV, vLayer));
    if (uPass == 0 && c.a < 0.5) discard;
    float bs = pow(vSky * uSun, 1.5);
    float bb = pow(vBlk, 1.2);
    vec3 lum = clamp(vec3(bs) + bb * vec3(1.15, 0.85, 0.5), vec3(0.028), vec3(1.0));
    float f = clamp((vDist - uFogStart) / (uFogEnd - uFogStart), 0.0, 1.0);
    vec3 rgb = mix(c.rgb * lum * vShade, uFog, f);
    o = vec4(rgb, uPass == 1 ? mix(uAlpha, 1.0, f) : 1.0);
})";

static const char* SKYOBJ_VS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUV;
uniform mat4 uVP;
out vec2 vUV;
void main(){ gl_Position = uVP * vec4(aPos, 1.0); vUV = aUV; })";

static const char* SKYOBJ_FS = R"(#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
uniform vec4 uColor;
out vec4 o;
void main(){ o = texture(uTex, vUV) * uColor; })";

static const char* LINE_VS = R"(#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uMVP;
void main(){ gl_Position = uMVP * vec4(aPos, 1.0); })";

static const char* LINE_FS = R"(#version 330 core
uniform vec4 uColor;
out vec4 o;
void main(){ o = uColor; })";

static const char* SPRITE_VS = R"(#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
uniform mat4 uProj;
out vec2 vUV;
void main(){ gl_Position = uProj * vec4(aPos, 0.0, 1.0); vUV = aUV; })";

static const char* SPRITE_FS = R"(#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
uniform vec4 uColor;
uniform int uUseTex;
uniform int uCutout;
out vec4 o;
void main(){
    vec4 c = (uUseTex == 1 ? texture(uTex, vUV) : vec4(1.0)) * uColor;
    if (uCutout == 1 && c.a < 0.5) discard;
    o = c;
})";

static const char* HUDARR_VS = R"(#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in float aLayer;
layout(location=3) in float aShade;
uniform mat4 uProj;
out vec2 vUV;
flat out float vLayer;
out float vShade;
void main(){ gl_Position = uProj * vec4(aPos, 0.0, 1.0); vUV = aUV; vLayer = aLayer; vShade = aShade; })";

static const char* HUDARR_FS = R"(#version 330 core
in vec2 vUV;
flat in float vLayer;
in float vShade;
uniform sampler2DArray uTex;
out vec4 o;
void main(){
    vec4 c = texture(uTex, vec3(vUV, vLayer));
    if (c.a < 0.1) discard;
    o = vec4(c.rgb * vShade, c.a);
})";

static const char* CLOUD_VS = R"(#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uVP;
uniform float uScroll;
uniform vec3 uCam;
out vec2 vUV;
out float vDist;
void main(){
    gl_Position = uVP * vec4(aPos, 1.0);
    vUV = (aPos.xz + vec2(uScroll, 0.0)) / 3072.0;
    vDist = distance(aPos.xz, uCam.xz);
})";

static const char* CLOUD_FS = R"(#version 330 core
in vec2 vUV;
in float vDist;
uniform sampler2D uTex;
uniform float uTint;
out vec4 o;
void main(){
    vec4 c = texture(uTex, vUV);
    if (c.a < 0.5) discard;
    float fade = clamp((vDist - 800.0) / 600.0, 0.0, 1.0);
    o = vec4(c.rgb * uTint, 0.75 * (1.0 - fade));
})";

// ---------------------------------------------------------------- state

static World g_world;
static Player g_player;
static Mobs g_mobs;
static bool g_captured = true;
static double g_lastMX = 0, g_lastMY = 0;
static bool g_firstMouse = true;
static int g_slot = 0;
static uint8_t g_hotbar[9] = {B_GRASS, B_DIRT, B_STONE, B_COBBLESTONE, B_OAK_PLANKS,
                              B_OAK_LOG, B_OAK_LEAVES, B_GLASS, B_TORCH};
static bool g_wantScreenshot = false;
static const double DAY_SECONDS = 600.0; // full day/night cycle length

static void keyCallback(GLFWwindow* w, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_ESCAPE) {
        g_captured = false;
        glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
    if (key == GLFW_KEY_F) g_player.flying = !g_player.flying;
    if (key == GLFW_KEY_F2) g_wantScreenshot = true;
    if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9) g_slot = key - GLFW_KEY_1;
}

static void cursorCallback(GLFWwindow*, double x, double y) {
    if (g_firstMouse) { g_lastMX = x; g_lastMY = y; g_firstMouse = false; }
    double dx = x - g_lastMX, dy = y - g_lastMY;
    g_lastMX = x; g_lastMY = y;
    if (!g_captured) return;
    g_player.yaw += (float)dx * 0.1f;
    g_player.pitch = std::clamp(g_player.pitch - (float)dy * 0.1f, -89.5f, 89.5f);
}

static void scrollCallback(GLFWwindow*, double, double dy) {
    g_slot = ((g_slot - (int)dy) % 9 + 9) % 9;
}

// ---------------------------------------------------------------- helpers

static std::string findAssetDir(const std::string& override_) {
    std::vector<fs::path> roots = {".", "..", "../.."};
    if (!override_.empty()) roots.insert(roots.begin(), override_);
    for (const auto& r : roots) {
        fs::path p = r / fs::path("Java Default Textures 1.21.7 - 1.21.8/assets/minecraft/textures");
        if (fs::exists(p / "block/dirt.png")) return p.string();
        if (fs::exists(fs::path(r) / "block/dirt.png")) return fs::path(r).string();
        if (fs::exists(fs::path(r) / "assets/minecraft/textures/block/dirt.png"))
            return (fs::path(r) / "assets/minecraft/textures").string();
    }
    throw std::runtime_error("could not find the texture pack directory (use --assets <path-to-textures-dir>)");
}

struct SpriteBatch {
    GLuint vao = 0, vbo = 0;
    void init() {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, 4096 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glBindVertexArray(0);
    }
    void draw(float x, float y, float w, float h, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1) {
        float v[] = {x, y, u0, v0, x + w, y, u1, v0, x + w, y + h, u1, v1,
                     x, y, u0, v0, x + w, y + h, u1, v1, x, y + h, u0, v1};
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof v, v);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
};

// Isometric block icon for the hotbar: top diamond + two side faces.
static void pushIsoBlock(std::vector<float>& out, float cx, float cy, float s, const BlockDef& d) {
    float hw = s * 0.5f, qh = s * 0.25f;
    auto quad = [&](const float p[4][2], uint16_t layer, float shade) {
        const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        int order[6] = {0, 1, 2, 0, 2, 3};
        for (int i : order)
            out.insert(out.end(), {p[i][0], p[i][1], uv[i][0], uv[i][1], (float)layer, shade});
    };
    if (d.cross) { // flat sprite icon for torches/plants
        float flat[4][2] = {{cx - hw, cy - hw}, {cx + hw, cy - hw}, {cx + hw, cy + hw}, {cx - hw, cy + hw}};
        quad(flat, d.tex[0], 1.0f);
        return;
    }
    float top[4][2] = {{cx, cy - hw}, {cx + hw, cy - qh}, {cx, cy}, {cx - hw, cy - qh}};
    float left[4][2] = {{cx - hw, cy - qh}, {cx, cy}, {cx, cy + hw}, {cx - hw, cy + qh}};
    float right[4][2] = {{cx, cy}, {cx + hw, cy - qh}, {cx + hw, cy + qh}, {cx, cy + hw}};
    quad(top, d.tex[2], 1.0f);
    quad(left, d.tex[4], 0.8f);
    quad(right, d.tex[0], 0.6f);
}

int main(int argc, char** argv) {
    // ------------------------------------------------------------ args
    std::string assetOverride, shotPath;
    int viewRadius = 10;
    long framesLimit = -1;
    uint32_t seed = std::random_device{}();
    glm::vec3 tpPos(0);
    float tpYaw = -90.0f, tpPitch = 0.0f;
    bool useTp = false, tpFly = false, findPeak = false, pokeTest = false, mobTest = false;
    float mobYaw = 90.0f; // --mobtest lineup facing: 90 = toward the camera, 0 = profile
    double timeOfDay = 0.08; // 0 = sunrise, 0.25 = noon, 0.5 = sunset, 0.75 = midnight
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--assets") assetOverride = next();
        else if (a == "--seed") seed = (uint32_t)std::stoul(next());
        else if (a == "--view") viewRadius = std::clamp(std::stoi(next()), 3, 32);
        else if (a == "--frames") framesLimit = std::stol(next());
        else if (a == "--shot") shotPath = next();
        else if (a == "--tp") { std::string s = next(); sscanf(s.c_str(), "%f,%f,%f", &tpPos.x, &tpPos.y, &tpPos.z); useTp = true; }
        else if (a == "--look") { std::string s = next(); sscanf(s.c_str(), "%f,%f", &tpYaw, &tpPitch); }
        else if (a == "--fly") tpFly = true;
        else if (a == "--findpeak") findPeak = true;
        else if (a == "--poke") pokeTest = true;
        else if (a == "--mobtest") mobTest = true;
        else if (a == "--mobyaw") mobYaw = std::stof(next());
        else if (a == "--timeofday") timeOfDay = std::stod(next());
    }

    std::string texDir;
    try {
        texDir = findAssetDir(assetOverride);
    } catch (const std::exception& e) {
        fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    // ------------------------------------------------------------ window
    if (!glfwInit()) { fprintf(stderr, "glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 0);
    GLFWwindow* win = glfwCreateWindow(1280, 720, "Minecraft C++", nullptr, nullptr);
    if (!win) { fprintf(stderr, "window creation failed\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    glewExperimental = GL_TRUE;
    GLenum glewErr = glewInit();
#ifdef GLEW_ERROR_NO_GLX_DISPLAY
    // On Wayland glewInit reports this even though GL loading succeeds.
    if (glewErr == GLEW_ERROR_NO_GLX_DISPLAY) glewErr = GLEW_OK;
#endif
    if (glewErr != GLEW_OK) { fprintf(stderr, "glewInit failed: %s\n", glewGetErrorString(glewErr)); return 1; }

    glfwSetKeyCallback(win, keyCallback);
    glfwSetCursorPosCallback(win, cursorCallback);
    glfwSetScrollCallback(win, scrollCallback);
    glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (glfwRawMouseMotionSupported()) glfwSetInputMode(win, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

    printf("Minecraft C++ | seed %u | view distance %d chunks\n", seed, viewRadius);
    printf("controls: WASD move, mouse look, Space jump, Ctrl sprint, F fly (Space/Shift up/down)\n");
    printf("          LMB break blocks / punch mobs, RMB place, MMB pick, 1-9/scroll hotbar (9 = torch!)\n");
    printf("          T fast-forward time, F2 screenshot, Esc release mouse\n");
    printf("          watch out for zombies and creepers at night...\n");

    // ------------------------------------------------------------ resources
    BlockAtlas atlas;
    atlas.blockDir = texDir + "/block";
    try {
        initBlocks(atlas);
    } catch (const std::exception& e) {
        fprintf(stderr, "error loading textures: %s\n", e.what());
        return 1;
    }
    atlas.upload();

    int chW, chH, hbW, hbH, hsW, hsH;
    GLuint texCrosshair = loadTexture2D(texDir + "/gui/sprites/hud/crosshair.png", &chW, &chH);
    GLuint texHotbar = loadTexture2D(texDir + "/gui/sprites/hud/hotbar.png", &hbW, &hbH);
    GLuint texHotbarSel = loadTexture2D(texDir + "/gui/sprites/hud/hotbar_selection.png", &hsW, &hsH);
    GLuint texClouds = loadTexture2D(texDir + "/environment/clouds.png", nullptr, nullptr, GL_REPEAT);
    GLuint texSun = loadTexture2D(texDir + "/environment/sun.png");
    GLuint texMoon = loadTexture2D(texDir + "/environment/moon_phases.png");
    GLuint texHeartBg = loadTexture2D(texDir + "/gui/sprites/hud/heart/container.png");
    GLuint texHeartFull = loadTexture2D(texDir + "/gui/sprites/hud/heart/full.png");
    GLuint texHeartHalf = loadTexture2D(texDir + "/gui/sprites/hud/heart/half.png");

    Shader chunkSh(CHUNK_VS, CHUNK_FS);
    Shader lineSh(LINE_VS, LINE_FS);
    Shader spriteSh(SPRITE_VS, SPRITE_FS);
    Shader hudArrSh(HUDARR_VS, HUDARR_FS);
    Shader cloudSh(CLOUD_VS, CLOUD_FS);
    Shader skyObjSh(SKYOBJ_VS, SKYOBJ_FS);
    g_mobs.init(texDir);

    // Selection wireframe cube
    static const float cubeLines[] = {
        0,0,0, 1,0,0,  1,0,0, 1,0,1,  1,0,1, 0,0,1,  0,0,1, 0,0,0,
        0,1,0, 1,1,0,  1,1,0, 1,1,1,  1,1,1, 0,1,1,  0,1,1, 0,1,0,
        0,0,0, 0,1,0,  1,0,0, 1,1,0,  1,0,1, 1,1,1,  0,0,1, 0,1,1};
    GLuint lineVAO, lineVBO;
    glGenVertexArrays(1, &lineVAO);
    glGenBuffers(1, &lineVBO);
    glBindVertexArray(lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, lineVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof cubeLines, cubeLines, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    // Cloud quad (rebuilt each frame around the camera)
    GLuint cloudVAO, cloudVBO;
    glGenVertexArrays(1, &cloudVAO);
    glGenBuffers(1, &cloudVBO);
    glBindVertexArray(cloudVAO);
    glBindBuffer(GL_ARRAY_BUFFER, cloudVBO);
    glBufferData(GL_ARRAY_BUFFER, 18 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    // Sun/moon billboards (pos3 + uv2, rebuilt each frame)
    GLuint skyVAO, skyVBO;
    glGenVertexArrays(1, &skyVAO);
    glGenBuffers(1, &skyVBO);
    glBindVertexArray(skyVAO);
    glBindBuffer(GL_ARRAY_BUFFER, skyVBO);
    glBufferData(GL_ARRAY_BUFFER, 60 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

    SpriteBatch sprite;
    sprite.init();

    // Hotbar icon batch (pos2, uv2, layer, shade)
    GLuint hudVAO, hudVBO;
    glGenVertexArrays(1, &hudVAO);
    glGenBuffers(1, &hudVBO);
    glBindVertexArray(hudVAO);
    glBindBuffer(GL_ARRAY_BUFFER, hudVBO);
    glBufferData(GL_ARRAY_BUFFER, 65536, nullptr, GL_DYNAMIC_DRAW);
    GLsizei hst = 6 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, hst, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, hst, (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, hst, (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, hst, (void*)(5 * sizeof(float)));
    glBindVertexArray(0);

    // ------------------------------------------------------------ world & spawn
    g_world.seed = seed;

    if (findPeak) {
        int bestH = 0, bx = 0, bz = 0;
        for (int z = -2000; z <= 2000; z += 8)
            for (int x = -2000; x <= 2000; x += 8) {
                int h = g_world.terrainHeight(x, z);
                if (h > bestH) { bestH = h; bx = x; bz = z; }
            }
        printf("highest peak: y=%d at %d,%d\n", bestH, bx, bz);
        tpPos = glm::vec3(bx + 0.5f, (float)(bestH + 30), bz + 0.5f);
        useTp = true;
        tpFly = true;
    }

    // Find dry land: spiral outward sampling the pure noise heightmap.
    int sx = 8, sz = 8;
    for (int ring = 0; ring < 300; ring++) {
        bool found = false;
        int r = ring * 8;
        for (int a = 0; a < std::max(1, ring * 4) && !found; a++) {
            float ang = (float)a / std::max(1, ring * 4) * 6.2831853f;
            int x = 8 + (int)(std::cos(ang) * r), z = 8 + (int)(std::sin(ang) * r);
            if (g_world.terrainHeight(x, z) >= SEA + 3) { sx = x; sz = z; found = true; }
        }
        if (found) break;
    }
    int scx = sx >> 4, scz = sz >> 4;
    for (int dz = -2; dz <= 2; dz++)
        for (int dx = -2; dx <= 2; dx++) g_world.ensure(scx + dx, scz + dz);
    // Nudge off trees/plants if the exact column is blocked.
    for (int tries = 0; tries < 32; tries++) {
        int h = g_world.terrainHeight(sx, sz);
        if (!blockSolid(g_world.block(sx, h + 1, sz)) && !blockSolid(g_world.block(sx, h + 2, sz))) break;
        sx += (tries % 2) ? 1 : -2;
        sz += 1;
    }
    g_player.pos = glm::vec3(sx + 0.5f, (float)(g_world.terrainHeight(sx, sz) + 1), sz + 0.5f);
    if (useTp) {
        g_player.pos = tpPos;
        g_player.yaw = tpYaw;
        g_player.pitch = tpPitch;
        int tcx = (int)std::floor(tpPos.x) >> 4, tcz = (int)std::floor(tpPos.z) >> 4;
        for (int dz = -1; dz <= 1; dz++)
            for (int dx = -1; dx <= 1; dx++) g_world.ensure(tcx + dx, tcz + dz);
    }
    g_player.flying = tpFly;

    glm::vec3 spawnPoint = g_player.pos;
    printf("spawn: %.1f %.1f %.1f\n", spawnPoint.x, spawnPoint.y, spawnPoint.z);
    double nowShared = 0;
    g_mobs.damagePlayer = [&](float dmg, glm::vec3 knock) {
        if (g_player.flying) return; // flying = creative mode: invulnerable
        g_player.health -= dmg;
        g_player.vel += knock;
        g_player.hurtFlash = 0.5f;
        g_player.lastDamage = nowShared;
        if (g_player.health <= 0) { // respawn
            g_player.pos = spawnPoint;
            g_player.vel = glm::vec3(0);
            g_player.health = 20.0f;
            g_player.hurtFlash = 1.2f;
        }
    };
    g_mobs.explosionAtPlayer = [&](glm::vec3 c, float radius) {
        glm::vec3 to = g_player.pos + glm::vec3(0, 0.9f, 0) - c;
        float d = glm::length(to);
        float maxD = radius * 2.0f;
        if (d >= maxD) return;
        float k = 1.0f - d / maxD;
        g_mobs.damagePlayer(26.0f * k,
                            glm::normalize(to + glm::vec3(0, 0.3f, 0)) * 12.0f * k + glm::vec3(0, 4.0f * k, 0));
    };
    if (mobTest) {
        int types[4] = {MOB_PIG, MOB_COW, MOB_ZOMBIE, MOB_CREEPER};
        for (int i = 0; i < 4; i++) {
            float mx = spawnPoint.x - 4.5f + i * 3.0f;
            float mz = spawnPoint.z - 6.0f;
            int ty = g_world.topSolidY((int)std::floor(mx), (int)std::floor(mz));
            if (ty < 0) ty = (int)spawnPoint.y;
            g_mobs.spawnAt(types[i], glm::vec3(mx, ty + 1.01f, mz));
            g_mobs.mobs.back().yaw = mobYaw;
            g_mobs.mobs.back().frozen = true; // hold the pose for screenshots
        }
    }

    if (framesLimit >= 0) { // test mode: report cave density around spawn
        long air = 0, tot = 0;
        for (int dz = -2; dz <= 2; dz++)
            for (int dx = -2; dx <= 2; dx++) {
                Chunk* c = g_world.chunk(scx + dx, scz + dz);
                if (!c) continue;
                for (int y = 6; y < 40; y++)
                    for (int z = 0; z < CZ; z++)
                        for (int x = 0; x < CX; x++) { tot++; air += c->get(x, y, z) == B_AIR; }
            }
        printf("cave air fraction below ground: %.2f%%\n", 100.0 * air / std::max(1L, tot));
    }
    g_world.processLight();

    const int GEN_R = viewRadius + 1;
    std::vector<glm::ivec2> offsets;
    for (int dz = -GEN_R; dz <= GEN_R; dz++)
        for (int dx = -GEN_R; dx <= GEN_R; dx++)
            if (dx * dx + dz * dz <= GEN_R * GEN_R) offsets.push_back({dx, dz});
    std::sort(offsets.begin(), offsets.end(), [](glm::ivec2 a, glm::ivec2 b) {
        return a.x * a.x + a.y * a.y < b.x * b.x + b.y * b.y;
    });

    // ------------------------------------------------------------ loop
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    double lastTime = glfwGetTime();
    double physAccum = 0, fpsTimer = 0, unloadTimer = 0;
    int fpsCount = 0, fps = 0;
    long frameIndex = 0;
    double lastBreak = -1, lastPlace = -1;
    glm::ivec3 selBlock, selPrev;
    bool hasSel = false;
    bool wasLeft = false, wasRight = false, wasMid = false;

    while (!glfwWindowShouldClose(win)) {
        double now = glfwGetTime();
        double dt = std::min(now - lastTime, 0.1);
        lastTime = now;
        timeOfDay += dt / DAY_SECONDS;
        if (g_captured && glfwGetKey(win, GLFW_KEY_T) == GLFW_PRESS)
            timeOfDay += dt * 40.0 / DAY_SECONDS; // fast-forward
        fpsTimer += dt; fpsCount++;
        if (fpsTimer >= 0.5) { fps = (int)(fpsCount / fpsTimer); fpsTimer = 0; fpsCount = 0; }

        glfwPollEvents();

        // ---- input
        if (!g_captured && glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
            g_captured = true;
            g_firstMouse = true;
            glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
        PlayerInput pin;
        if (g_captured) {
            pin.forward = glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS;
            pin.back = glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS;
            pin.left = glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS;
            pin.right = glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS;
            pin.jump = glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS;
            pin.down = glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
            pin.sprint = glfwGetKey(win, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;
        }

        // ---- physics (fixed step)
        physAccum += dt;
        const float STEP = 1.0f / 120.0f;
        int steps = 0;
        while (physAccum >= STEP && steps++ < 10) {
            g_player.update(g_world, pin, STEP);
            physAccum -= STEP;
        }

        // ---- day/night factors (used by mobs and rendering)
        nowShared = now;
        auto sstep = [](float e0, float e1, float x) {
            float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        };
        double dayFrac = timeOfDay - std::floor(timeOfDay);
        float sunAngle = (float)(dayFrac * 2.0 * 3.14159265358979);
        float elev = std::sin(sunAngle);
        float sunF = 0.14f + 0.86f * sstep(-0.06f, 0.25f, elev); // daylight factor
        float sunNorm = (sunF - 0.14f) / 0.86f;

        // ---- mobs + player vitals
        g_mobs.update(g_world, g_player.pos, (float)dt, sunF, !mobTest);
        g_player.hurtFlash = std::max(0.0f, g_player.hurtFlash - (float)dt * 1.1f);
        if (now - g_player.lastDamage > 8.0 && g_player.health < 20.0f)
            g_player.health = std::min(20.0f, g_player.health + (float)dt * 0.6f);

        // ---- selection + block actions
        glm::vec3 eye = g_player.eyePos();
        hasSel = g_world.raycast(eye, g_player.front(), 5.0f, selBlock, selPrev);

        bool leftDown = g_captured && glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        bool rightDown = g_captured && glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        bool midDown = g_captured && glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
        // punching a mob takes priority over the block behind it
        int mobIdx = -1;
        float mobT = 0;
        bool mobHit = g_captured && g_mobs.raycast(eye, g_player.front(), 4.5f, mobIdx, mobT);
        if (mobHit && hasSel &&
            glm::length(glm::vec3(selBlock) + glm::vec3(0.5f) - eye) < mobT) mobHit = false;
        if (mobHit && leftDown && (!wasLeft || now - lastBreak > 0.3)) {
            g_mobs.hurt(mobIdx, 4.0f, g_player.front());
            lastBreak = now;
        } else if (hasSel) {
            if (leftDown && (!wasLeft || now - lastBreak > 0.25)) {
                g_world.setBlock(selBlock.x, selBlock.y, selBlock.z, B_AIR);
                lastBreak = now;
            }
            if (rightDown && (!wasRight || now - lastPlace > 0.25)) {
                uint8_t tgt = g_world.block(selPrev.x, selPrev.y, selPrev.z);
                uint8_t put = g_hotbar[g_slot];
                bool replaceable = tgt == B_AIR || g_blocks[tgt].fluid || g_blocks[tgt].cross;
                bool supported = !g_blocks[put].cross ||
                                 blockSolid(g_world.block(selPrev.x, selPrev.y - 1, selPrev.z));
                if (replaceable && supported && !(g_blocks[put].solid && g_player.intersectsBlock(selPrev)))
                    g_world.setBlock(selPrev.x, selPrev.y, selPrev.z, put);
                lastPlace = now;
            }
            if (midDown && !wasMid) {
                uint8_t b = g_world.block(selBlock.x, selBlock.y, selBlock.z);
                if (b != B_AIR) g_hotbar[g_slot] = b == B_SNOWY_GRASS ? B_GRASS : b;
            }
        }
        wasLeft = leftDown; wasRight = rightDown; wasMid = midDown;

        static glm::ivec3 pokeTorchPos;
        static int pokeStage = 0, pokeDigs = 0;
        if (pokeTest && hasSel && frameIndex >= 100 && pokeDigs < 3) {
            g_world.setBlock(selBlock.x, selBlock.y, selBlock.z, B_AIR); // dig a short hole
            pokeDigs++;
        }
        if (pokeTest && pokeStage == 0 && frameIndex >= 200 && hasSel) {
            g_world.setBlock(selPrev.x, selPrev.y, selPrev.z, B_TORCH);
            pokeTorchPos = selPrev;
            pokeStage = 1;
        }
        if (pokeTest && pokeStage == 1 && frameIndex >= 280) {
            g_world.setBlock(pokeTorchPos.x, pokeTorchPos.y, pokeTorchPos.z, B_AIR); // removal test
            pokeStage = 2;
        }

        // ---- chunk streaming
        int pcx = (int)std::floor(g_player.pos.x) >> 4;
        int pcz = (int)std::floor(g_player.pos.z) >> 4;
        {
            double budget = frameIndex < 40 ? 0.040 : 0.006;
            double t0 = glfwGetTime();
            for (const auto& o : offsets) {
                Chunk* c = g_world.chunk(pcx + o.x, pcz + o.y);
                if (!c || !c->generated) {
                    g_world.ensure(pcx + o.x, pcz + o.y);
                    if (glfwGetTime() - t0 > budget) break;
                }
            }
            g_world.processLight();
            for (const auto& o : offsets) {
                if (o.x * o.x + o.y * o.y > viewRadius * viewRadius) continue;
                if (glfwGetTime() - t0 > budget * 2) break;
                Chunk* c = g_world.chunk(pcx + o.x, pcz + o.y);
                if (c && c->generated && c->dirty && g_world.neighborsGenerated(c->cx, c->cz))
                    buildChunkMesh(g_world, *c);
            }
        }
        unloadTimer += dt;
        if (unloadTimer > 3.0) {
            unloadTimer = 0;
            g_world.unloadFar(pcx, pcz, GEN_R + 2);
        }

        atlas.update(now);

        // ---- render world
        int fbW, fbH;
        glfwGetFramebufferSize(win, &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);

        // Sky colors for this time of day
        glm::vec3 skyColor = glm::mix(glm::vec3(0.02f, 0.03f, 0.08f),
                                      glm::vec3(0.55f, 0.72f, 1.0f), sstep(-0.18f, 0.22f, elev));
        skyColor = glm::mix(skyColor, glm::vec3(1.0f, 0.55f, 0.30f),
                            std::exp(-std::fabs(elev) * 9.0f) * 0.55f);
        float fogEnd = (float)(viewRadius - 1) * 16.0f;
        float fogStart = fogEnd * 0.55f;
        if (g_player.eyeInWater) {
            skyColor = glm::vec3(0.05f, 0.18f, 0.42f) * (0.25f + 0.75f * sunNorm);
            fogStart = 0.0f;
            fogEnd = 28.0f;
        }
        glClearColor(skyColor.r, skyColor.g, skyColor.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 proj = glm::perspective(glm::radians(75.0f), (float)fbW / (float)fbH, 0.05f, 1500.0f);
        glm::mat4 view = glm::lookAt(eye, eye + g_player.front(), glm::vec3(0, 1, 0));
        glm::mat4 vp = proj * view;
        Frustum frustum;
        frustum.fromMatrix(vp);

        // Sun and moon
        if (!g_player.eyeInWater) {
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glDisable(GL_CULL_FACE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            glm::vec3 sdir = glm::normalize(glm::vec3(std::cos(sunAngle), std::sin(sunAngle), 0.25f));
            auto billboard = [&](glm::vec3 dir, float size, float u0, float v0, float u1, float v1) {
                glm::vec3 center = eye + dir * 420.0f;
                glm::vec3 r = glm::normalize(glm::cross(dir, glm::vec3(0, 1, 0))) * size;
                glm::vec3 up = glm::normalize(glm::cross(r, dir)) * size;
                glm::vec3 A = center - r - up, B = center + r - up, C = center + r + up, D = center - r + up;
                float q[30] = {A.x, A.y, A.z, u0, v1, B.x, B.y, B.z, u1, v1, C.x, C.y, C.z, u1, v0,
                               A.x, A.y, A.z, u0, v1, C.x, C.y, C.z, u1, v0, D.x, D.y, D.z, u0, v0};
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof q, q);
                glDrawArrays(GL_TRIANGLES, 0, 6);
            };
            skyObjSh.use();
            skyObjSh.set("uVP", vp);
            skyObjSh.set("uTex", 0);
            glActiveTexture(GL_TEXTURE0);
            glBindVertexArray(skyVAO);
            glBindBuffer(GL_ARRAY_BUFFER, skyVBO);
            if (elev > -0.35f) {
                skyObjSh.set("uColor", glm::vec4(1));
                glBindTexture(GL_TEXTURE_2D, texSun);
                billboard(sdir, 42.0f, 0, 0, 1, 1);
            }
            if (elev < 0.35f) {
                skyObjSh.set("uColor", glm::vec4(1, 1, 1, 0.9f));
                glBindTexture(GL_TEXTURE_2D, texMoon);
                billboard(-sdir, 30.0f, 0, 0, 0.25f, 0.5f); // first moon phase
            }
            glDisable(GL_BLEND);
            glEnable(GL_CULL_FACE);
            glDepthMask(GL_TRUE);
            glEnable(GL_DEPTH_TEST);
        }

        chunkSh.use();
        chunkSh.set("uVP", vp);
        chunkSh.set("uCam", eye);
        chunkSh.set("uTex", 0);
        chunkSh.set("uFog", skyColor);
        chunkSh.set("uFogStart", fogStart);
        chunkSh.set("uFogEnd", fogEnd);
        chunkSh.set("uSun", sunF);
        chunkSh.set("uPass", 0);
        chunkSh.set("uAlpha", 1.0f);
        atlas.bind(0);

        std::vector<std::pair<float, Chunk*>> waterChunks;
        for (const auto& o : offsets) {
            if (o.x * o.x + o.y * o.y > viewRadius * viewRadius) continue;
            Chunk* c = g_world.chunk(pcx + o.x, pcz + o.y);
            if (!c) continue;
            glm::vec3 mn((float)(c->cx * CX), 0.0f, (float)(c->cz * CZ));
            glm::vec3 mx = mn + glm::vec3(CX, CY, CZ);
            if (!frustum.aabbVisible(mn, mx)) continue;
            if (c->indexCount) {
                chunkSh.set("uOrigin", mn);
                glBindVertexArray(c->vao);
                glDrawElements(GL_TRIANGLES, c->indexCount, GL_UNSIGNED_INT, nullptr);
            }
            if (c->windexCount) {
                glm::vec2 center(mn.x + 8.0f - eye.x, mn.z + 8.0f - eye.z);
                waterChunks.push_back({glm::dot(center, center), c});
            }
        }

        // Selection outline
        if (hasSel) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            lineSh.use();
            glm::mat4 model = glm::translate(glm::mat4(1), glm::vec3(selBlock) - 0.002f);
            model = glm::scale(model, glm::vec3(1.004f));
            lineSh.set("uMVP", vp * model);
            lineSh.set("uColor", glm::vec4(0.05f, 0.05f, 0.05f, 0.7f));
            glBindVertexArray(lineVAO);
            glDrawArrays(GL_LINES, 0, 24);
            glDisable(GL_BLEND);
        }

        // Mobs
        g_mobs.render(vp, eye, g_world, sunF, skyColor, fogStart, fogEnd);

        // Clouds
        {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            glDisable(GL_CULL_FACE);
            cloudSh.use();
            cloudSh.set("uVP", vp);
            cloudSh.set("uCam", eye);
            cloudSh.set("uScroll", (float)(now * 1.5));
            cloudSh.set("uTint", 0.2f + 0.8f * sunNorm);
            cloudSh.set("uTex", 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texClouds);
            float cy = 112.0f, R = 1536.0f;
            float cq[] = {eye.x - R, cy, eye.z - R, eye.x + R, cy, eye.z - R, eye.x + R, cy, eye.z + R,
                          eye.x - R, cy, eye.z - R, eye.x + R, cy, eye.z + R, eye.x - R, cy, eye.z + R};
            glBindVertexArray(cloudVAO);
            glBindBuffer(GL_ARRAY_BUFFER, cloudVBO);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof cq, cq);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glDepthMask(GL_TRUE);
            glEnable(GL_CULL_FACE);
            glDisable(GL_BLEND);
        }

        // Water (back to front, double-sided)
        if (!waterChunks.empty()) {
            std::sort(waterChunks.begin(), waterChunks.end(),
                      [](const auto& a, const auto& b) { return a.first > b.first; });
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDisable(GL_CULL_FACE);
            chunkSh.use();
            chunkSh.set("uPass", 1);
            chunkSh.set("uAlpha", 0.68f);
            atlas.bind(0);
            for (auto& [d2, c] : waterChunks) {
                chunkSh.set("uOrigin", glm::vec3((float)(c->cx * CX), 0.0f, (float)(c->cz * CZ)));
                glBindVertexArray(c->wvao);
                glDrawElements(GL_TRIANGLES, c->windexCount, GL_UNSIGNED_INT, nullptr);
            }
            glEnable(GL_CULL_FACE);
            glDisable(GL_BLEND);
        }

        // ---- HUD
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glm::mat4 ortho = glm::ortho(0.0f, (float)fbW, (float)fbH, 0.0f);
        int s = std::clamp(fbH / 240, 2, 4);

        if (g_player.eyeInWater) {
            spriteSh.use();
            spriteSh.set("uProj", ortho);
            spriteSh.set("uUseTex", 0);
            spriteSh.set("uCutout", 0);
            spriteSh.set("uColor", glm::vec4(0.1f, 0.2f, 0.55f, 0.35f));
            sprite.draw(0, 0, (float)fbW, (float)fbH);
        }
        if (g_player.hurtFlash > 0) {
            spriteSh.use();
            spriteSh.set("uProj", ortho);
            spriteSh.set("uUseTex", 0);
            spriteSh.set("uCutout", 0);
            spriteSh.set("uColor", glm::vec4(0.75f, 0.0f, 0.0f, std::min(0.4f, g_player.hurtFlash)));
            sprite.draw(0, 0, (float)fbW, (float)fbH);
        }

        spriteSh.use();
        spriteSh.set("uProj", ortho);
        spriteSh.set("uTex", 0);
        spriteSh.set("uUseTex", 1);
        spriteSh.set("uColor", glm::vec4(1));
        glActiveTexture(GL_TEXTURE0);

        // hotbar background + selection
        float hbx = (fbW - hbW * s) / 2.0f, hby = (float)(fbH - hbH * s);
        spriteSh.set("uCutout", 1);
        glBindTexture(GL_TEXTURE_2D, texHotbar);
        sprite.draw(hbx, hby, (float)(hbW * s), (float)(hbH * s));
        glBindTexture(GL_TEXTURE_2D, texHotbarSel);
        sprite.draw(hbx - 1.0f * s + g_slot * 20.0f * s, (float)fbH - hsH * (float)s,
                    (float)(hsW * s), (float)(hsH * s));

        // hearts
        {
            float hy = hby - 10.0f * s;
            glBindTexture(GL_TEXTURE_2D, texHeartBg);
            for (int i = 0; i < 10; i++)
                sprite.draw(hbx + i * 8.0f * s, hy, 9.0f * s, 9.0f * s);
            int hp = (int)std::ceil(g_player.health);
            for (int i = 0; i < 10; i++) {
                if (hp >= i * 2 + 2) glBindTexture(GL_TEXTURE_2D, texHeartFull);
                else if (hp >= i * 2 + 1) glBindTexture(GL_TEXTURE_2D, texHeartHalf);
                else continue;
                sprite.draw(hbx + i * 8.0f * s, hy, 9.0f * s, 9.0f * s);
            }
        }

        // crosshair (inverted blend, vanilla style)
        glBlendFuncSeparate(GL_ONE_MINUS_DST_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_ONE, GL_ZERO);
        glBindTexture(GL_TEXTURE_2D, texCrosshair);
        sprite.draw(std::floor((fbW - chW * s) / 2.0f), std::floor((fbH - chH * s) / 2.0f),
                    (float)(chW * s), (float)(chH * s));
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // hotbar block icons
        {
            std::vector<float> icons;
            for (int i = 0; i < 9; i++) {
                float icx = hbx + (11.0f + 20.0f * i) * s;
                float icy = fbH - 11.0f * s;
                pushIsoBlock(icons, icx, icy, 13.0f * s, g_blocks[g_hotbar[i]]);
            }
            hudArrSh.use();
            hudArrSh.set("uProj", ortho);
            hudArrSh.set("uTex", 0);
            atlas.bind(0);
            glBindVertexArray(hudVAO);
            glBindBuffer(GL_ARRAY_BUFFER, hudVBO);
            glBufferSubData(GL_ARRAY_BUFFER, 0, icons.size() * sizeof(float), icons.data());
            glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(icons.size() / 6));
        }

        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);

        // ---- screenshot / test hooks
        if (g_wantScreenshot || (framesLimit >= 0 && frameIndex == framesLimit - 1 && !shotPath.empty())) {
            std::vector<uint8_t> px((size_t)fbW * fbH * 4);
            glReadPixels(0, 0, fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            std::string path = shotPath;
            if (g_wantScreenshot || path.empty()) {
                char name[64];
                time_t t = time(nullptr);
                strftime(name, sizeof name, "screenshot_%Y%m%d_%H%M%S.png", localtime(&t));
                path = name;
            }
            savePNG(path, fbW, fbH, px.data());
            printf("saved %s\n", path.c_str());
            g_wantScreenshot = false;
        }

        glfwSwapBuffers(win);

        // ---- window title
        static double titleTimer = 0;
        titleTimer += dt;
        if (titleTimer > 0.25) {
            titleTimer = 0;
            int mins = ((int)(dayFrac * 24 * 60) + 6 * 60) % (24 * 60);
            char t[256];
            snprintf(t, sizeof t, "Minecraft C++ | %d fps | %02d:%02d | %.1f / %.1f / %.1f | holding: %s%s",
                     fps, mins / 60, mins % 60, g_player.pos.x, g_player.pos.y, g_player.pos.z,
                     g_blocks[g_hotbar[g_slot]].name, g_player.flying ? " | flying" : "");
            glfwSetWindowTitle(win, t);
        }

        frameIndex++;
        if (framesLimit >= 0 && frameIndex >= framesLimit) break;
    }

    if (framesLimit >= 0) {
        long long totalIdx = 0;
        for (auto& [k, c] : g_world.chunks) totalIdx += c->indexCount + c->windexCount;
        printf("ran %ld frames in %.1fs (%.0f fps avg); %lld total mesh indices (%lld triangles); %zu mobs alive\n",
               frameIndex, glfwGetTime(), frameIndex / glfwGetTime(), totalIdx, totalIdx / 3, g_mobs.mobs.size());
    }
    glfwTerminate();
    return 0;
}
