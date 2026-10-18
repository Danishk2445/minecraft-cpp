// Options.h -- the player's settings (vanilla Options), saved as options.json in
// the game directory. Values outside their range are clamped on load.
#pragma once

#include <filesystem>
#include <string>

namespace mc {

enum class CloudMode : uint8_t { Off, Fast, Fancy };
enum class ParticleMode : uint8_t { All, Decreased, Minimal };
enum class ChatVisibility : uint8_t { Full, Commands, Hidden };
enum class AttackIndicator : uint8_t { Off, Crosshair, Hotbar };

struct Options {
    // Video
    int fov = 70;             // 30..110 degrees
    float fovEffectScale = 1.0f; // vanilla "FOV Effects" (accessibility): how much sprinting, flying and bows widen or narrow it
    int renderDistance = 12;  // 2..32 chunks
    int guiScale = 0;         // 0 = auto, else 1..max
    int maxFps = 120;         // 10..260; 260 = unlimited
    bool vsync = true;
    bool fullscreen = false;
    bool smoothLighting = true;
    float gamma = 0.5f;       // brightness: 0 Moody .. 1 Bright
    bool entityShadows = true;
    bool viewBobbing = true;
    CloudMode clouds = CloudMode::Fancy;
    ParticleMode particles = ParticleMode::All;
    int menuBlur = 5;         // menu background blurriness, 0..10
    AttackIndicator attackIndicator = AttackIndicator::Crosshair;
    // Controls
    float mouseSensitivity = 0.5f; // 0..1
    bool invertMouse = false;
    // Chat
    ChatVisibility chatVisibility = ChatVisibility::Full;
    float chatOpacity = 1.0f;          // 0.1..1
    float textBackgroundOpacity = 0.5f; // 0..1
    float chatScale = 1.0f;             // 0..1

    // Degrees per mouse count (vanilla MouseHandler: (0.6 s + 0.2)^3 * 8 * 0.15).
    double mouseDegreesPerCount() const {
        const double d = mouseSensitivity * 0.6 + 0.2;
        return d * d * d * 8.0 * 0.15;
    }
    bool unlimitedFps() const { return maxFps >= 260; }

    // Missing or unreadable files keep the defaults.
    static Options load(const std::filesystem::path& file, std::string* error = nullptr);
    bool save(const std::filesystem::path& file, std::string* error = nullptr) const;
    std::string toJson() const;
    static Options fromJson(std::string_view text, std::string* error = nullptr);
};

} // namespace mc
