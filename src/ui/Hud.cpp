#include "ui/Hud.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "entity/Player.h"
#include "ui/GuiGraphics.h"

namespace mc {

void Hud::tick(const Player& player, const ItemNamer& name) {
    ++ticks_;
    if (overlayTime_ > 0) --overlayTime_;
    random_ = random_ * 1664525u + 1013904223u;
    // Vanilla renderPlayerHealth's blink (its second of real time as 20 ticks here).
    const int health = int(std::ceil(player.health));
    if (health < lastHealth_ && player.invulnerableTime > 0) {
        lastHealthTime_ = ticks_;
        healthBlinkTime_ = ticks_ + 20;
    } else if (health > lastHealth_ && player.invulnerableTime > 0) {
        lastHealthTime_ = ticks_;
        healthBlinkTime_ = ticks_ + 10;
    }
    if (ticks_ - lastHealthTime_ > 20) {
        displayHealth_ = health;
        lastHealthTime_ = ticks_;
    }
    lastHealth_ = health;
    const ItemStack& held = player.inventory.selectedStack();
    if (held.empty()) {
        highlightTimer_ = 0;
        highlightItem_ = nullptr;
        return;
    }
    const std::string n = name(held);
    // A new item (or name) shows its name for 40 ticks, fading over the last 10.
    if (!highlightItem_ || held.item != highlightItem_ || n != highlightName_) highlightTimer_ = 40;
    else if (highlightTimer_ > 0) --highlightTimer_;
    highlightItem_ = held.item;
    highlightName_ = n;
}

void Hud::render(GuiGraphics& g, const Player& player, bool firstPerson, bool eyeInWater, const ItemDrawer& drawItem) {
    eyeInWater_ = eyeInWater;
    const glm::vec2 size = g.size();
    const bool spectator = player.mode() == GameMode::Spectator;
    // The crosshair inverts what is under it (first person only).
    if (firstPerson && !spectator) {
        g.batch().setBlend(GuiBatch::Blend::Invert);
        g.sprite("hud/crosshair", std::floor((size.x - 15) / 2), std::floor((size.y - 15) / 2), 15, 15);
        // Vanilla renderCrosshair: the attack strength as a bar under it while it recovers,
        // a sword when a living target can be hit at full strength.
        const float strength = player.attackStrengthScale(0.0f);
        const float ix = std::floor(size.x / 2) - 8, iy = std::floor(size.y / 2) - 7 + 16;
        if (attackIndicator == AttackIndicator::Crosshair && attackReady) {
            g.sprite("hud/crosshair_attack_indicator_full", ix, iy, 16, 16);
        } else if (attackIndicator == AttackIndicator::Crosshair && strength < 1.0f) {
            const float x = ix, y = iy;
            g.sprite("hud/crosshair_attack_indicator_background", x, y, 16, 4);
            g.spriteRegion("hud/crosshair_attack_indicator_progress", 16, 4, 0, 0, x, y, float(int(strength * 17.0f)), 4);
        }
        g.batch().setBlend(GuiBatch::Blend::Alpha);
    }
    renderSleepOverlay(g, player);
    renderOverlayMessage(g);
    if (spectator) return; // the spectator hotbar (teleport menu) is not in scope
    renderHotbar(g, player, drawItem);
    const bool survival = player.mode() == GameMode::Survival || player.mode() == GameMode::Adventure;
    if (survival) {
        renderStatus(g, player);
        renderExperience(g, player);
    }
    renderSelectedName(g, player);
}

void Hud::renderHotbar(GuiGraphics& g, const Player& player, const ItemDrawer& drawItem) {
    const float cx = std::floor(g.size().x / 2), h = g.size().y;
    const Inventory& inv = player.inventory;
    g.sprite("hud/hotbar", cx - 91, h - 22, 182, 22);
    g.sprite("hud/hotbar_selection", cx - 91 - 1 + float(inv.selected) * 20, h - 22 - 1, 24, 23);
    // A right-handed player's off hand is on the left.
    if (!inv.offhand.empty()) g.sprite("hud/hotbar_offhand_left", cx - 91 - 29, h - 23, 29, 24);
    for (int i = 0; i < Inventory::kHotbarSize; ++i)
        if (!inv.main[size_t(i)].empty()) drawItem(inv.main[size_t(i)], cx - 90 + float(i) * 20 + 2, h - 16 - 3);
    if (!inv.offhand.empty()) drawItem(inv.offhand, cx - 91 - 26, h - 16 - 3);
    // The hotbar attack indicator, opposite the off hand, fills from the bottom.
    const float strength = player.attackStrengthScale(0.0f);
    if (attackIndicator == AttackIndicator::Hotbar && strength < 1.0f) {
        const float x = cx + 91 + 6, y = h - 20;
        const int p = int(strength * 19.0f);
        g.sprite("hud/hotbar_attack_indicator_background", x, y, 18, 18);
        g.spriteRegion("hud/hotbar_attack_indicator_progress", 18, 18, 0, 18 - p, x, y + float(18 - p), 18, float(p));
    }
}

void Hud::renderStatus(GuiGraphics& g, const Player& player) {
    const float cx = std::floor(g.size().x / 2), h = g.size().y;
    const float left = cx - 91, right = cx + 91, top = h - 39;
    const int health = int(std::ceil(player.health));
    const float maxHealth = std::max(player.maxHealth, float(health));
    const int rows = int(std::ceil(maxHealth / 2.0f / 10.0f));
    const int rowHeight = std::max(10 - (rows - 2), 3);
    uint32_t rnd = random_;
    auto nextBit = [&](int bound) {
        rnd = rnd * 1103515245u + 12345u;
        return int((rnd >> 16) % uint32_t(bound));
    };

    // Armor above the hearts.
    if (player.armorValue > 0) {
        const float y = top - float(rows - 1) * float(rowHeight) - 10;
        for (int i = 0; i < 10; ++i) {
            const char* id = i * 2 + 1 < player.armorValue    ? "hud/armor_full"
                             : i * 2 + 1 == player.armorValue ? "hud/armor_half"
                                                              : "hud/armor_empty";
            g.sprite(id, left + float(i) * 8, y, 9, 9);
        }
    }
    // Hearts (vanilla renderHearts): containers, then full or half hearts; they shake at 4
    // health or less. While blinking, the containers light up and the health just lost shows
    // in pale blinking hearts under what is left.
    const bool blink = healthBlinkTime_ > ticks_ && (healthBlinkTime_ - ticks_) / 3 % 2 == 1;
    auto heart = [&](std::string_view kind, bool half, bool blinking) {
        std::string id = "hud/heart/";
        if (kind == "container") {
            id += hardcore ? "container_hardcore" : "container";
        } else {
            if (hardcore) id += "hardcore_";
            id += half ? "half" : "full";
        }
        if (blinking) id += "_blinking";
        return id;
    };
    const int containers = int(std::ceil(maxHealth / 2.0f));
    for (int i = containers - 1; i >= 0; --i) {
        const float x = left + float(i % 10) * 8;
        float y = top - float(i / 10) * float(rowHeight);
        if (health <= 4) y += float(nextBit(2));
        g.sprite(heart("container", false, blink), x, y, 9, 9);
        if (blink && i * 2 < displayHealth_) g.sprite(heart("normal", i * 2 + 1 == displayHealth_, true), x, y, 9, 9);
        if (i * 2 < health) g.sprite(heart("normal", i * 2 + 1 == health, false), x, y, 9, 9);
    }
    // Food from the right; it jitters when saturation is gone.
    for (int i = 0; i < 10; ++i) {
        float y = top;
        if (player.food.saturation <= 0.0f && ticks_ % (player.food.foodLevel * 3 + 1) == 0) y += float(nextBit(3) - 1);
        const float x = right - float(i) * 8 - 9;
        g.sprite("hud/food_empty", x, y, 9, 9);
        if (i * 2 + 1 < player.food.foodLevel) g.sprite("hud/food_full", x, y, 9, 9);
        if (i * 2 + 1 == player.food.foodLevel) g.sprite("hud/food_half", x, y, 9, 9);
    }
    // Air bubbles above the food while the eyes are underwater or the air is refilling
    // (vanilla renderAirBubbles).
    const int maxAir = player.maxAirSupply, air = std::clamp(player.airSupply, 0, maxAir);
    if (eyeInWater_ || air < maxAir) {
        auto bubbles = [&](int offset) { return int(std::ceil(float((air + offset) * 10) / float(maxAir))); };
        const int full = bubbles(-2), popping = bubbles(0), empty = 10 - bubbles(0);
        const float y = top - 10;
        for (int i = 1; i <= 10; ++i) {
            const float x = right - float(i - 1) * 8 - 9;
            if (i <= full) {
                g.sprite("hud/air", x, y, 9, 9);
            } else if (full != popping && i == popping && eyeInWater_) {
                g.sprite("hud/air_bursting", x, y, 9, 9);
            } else if (i > 10 - empty) {
                const float delay = empty == 10 && ticks_ % 2 == 0 ? float(nextBit(2)) : 0.0f;
                g.sprite("hud/air_empty", x, y + delay, 9, 9);
            }
        }
    }
}

void Hud::renderExperience(GuiGraphics& g, const Player& player) {
    const float cx = std::floor(g.size().x / 2), h = g.size().y;
    const float x = cx - 91, y = h - 32 + 3;
    g.sprite("hud/experience_bar_background", x, y, 182, 5);
    const int filled = int(player.experienceProgress * 183.0f);
    if (filled > 0) g.spriteRegion("hud/experience_bar_progress", 182, 5, 0, 0, x, y, float(filled), 5);
    if (player.experienceLevel > 0) {
        // The level in green with a black outline.
        const std::string level = std::to_string(player.experienceLevel);
        const float lx = std::floor((g.size().x - float(g.textWidth(level))) / 2), ly = h - 31 - 4;
        for (const glm::vec2 d : {glm::vec2(1, 0), glm::vec2(-1, 0), glm::vec2(0, 1), glm::vec2(0, -1)})
            g.text(level, lx + d.x, ly + d.y, 0xFF000000, false);
        g.text(level, lx, ly, 0xFF80FF20, false);
    }
}

void Hud::renderSelectedName(GuiGraphics& g, const Player& player) {
    if (highlightTimer_ <= 0 || highlightName_.empty()) return;
    const int alpha = std::min(255, int(float(highlightTimer_) * 256.0f / 10.0f));
    if (alpha <= 0) return;
    const float w = float(g.textWidth(highlightName_));
    float y = g.size().y - 59;
    if (player.mode() == GameMode::Creative) y += 14; // no health bar to clear
    g.text(highlightName_, std::floor((g.size().x - w) / 2), y, (uint32_t(alpha) << 24) | 0xFFFFFF);
}

void Hud::renderOverlayMessage(GuiGraphics& g) {
    if (overlayTime_ <= 0 || overlayMessage_.empty()) return;
    const int alpha = std::min(255, int(float(overlayTime_) * 255.0f / 20.0f));
    if (alpha <= 8) return;
    const float w = float(g.textWidth(overlayMessage_));
    g.text(overlayMessage_, std::floor(g.size().x / 2) - std::floor(w / 2), g.size().y - 68 - 4,
           (uint32_t(alpha) << 24) | 0xFFFFFF);
}

void Hud::renderSleepOverlay(GuiGraphics& g, const Player& player) {
    if (player.sleepTimer <= 0) return;
    const float t = float(player.sleepTimer);
    float f = t / 100.0f;
    if (f > 1.0f) f = 1.0f - (t - 100.0f) / 10.0f;
    g.fill(0, 0, g.size().x, g.size().y, (uint32_t(int(220.0f * f)) << 24) | 0x101020);
}

} // namespace mc
