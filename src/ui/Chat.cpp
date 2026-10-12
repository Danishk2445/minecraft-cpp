#include "ui/Chat.h"

#include <algorithm>
#include <cmath>

#include "game/Options.h"
#include "ui/GuiGraphics.h"

namespace mc {

void Chat::addMessage(std::string text, long long tick) {
    text_.push_front(text);
    if (text_.size() > kMaxMessages) text_.pop_back();
    if (lastGraphics_) {
        const auto lines = lastGraphics_->font().split(text, float(wrapWidth_));
        for (const std::string& l : lines) messages_.push_front({l, tick});
    } else {
        messages_.push_front({std::move(text), tick});
    }
    while (messages_.size() > kMaxMessages) messages_.pop_back();
}

void Chat::addSent(const std::string& line) {
    if (sent_.empty() || sent_.back() != line) sent_.push_back(line);
}

void Chat::scroll(int lines) {
    scroll_ = std::clamp(scroll_ + lines, 0, std::max(0, int(messages_.size()) - 20));
}

void Chat::render(GuiGraphics& g, const Options& options, long long tick, bool open) {
    lastGraphics_ = &g;
    if (options.chatVisibility == ChatVisibility::Hidden) return;
    const int width = int(std::floor(1.0 * 280.0 + 40.0)); // chat width option 1.0
    wrapWidth_ = width;
    const int maxLines = open ? 20 : 10;
    const float bottom = g.size().y - 40.0f;
    const float textAlpha = options.chatOpacity * 0.9f + 0.1f;
    const float backgroundAlpha = options.textBackgroundOpacity;
    int shown = 0;
    for (size_t i = size_t(open ? scroll_ : 0); i < messages_.size() && shown < maxLines; ++i) {
        const Line& line = messages_[i];
        float fade = 1.0f;
        if (!open) {
            // Vanilla getTimeFactor: full for 180 ticks, then fades out over 20.
            const double age = double(tick - line.tick);
            if (age >= 200.0) break;
            double d = std::clamp((1.0 - age / 200.0) * 10.0, 0.0, 1.0);
            fade = float(d * d);
        }
        const float y = bottom - float(shown + 1) * 9.0f;
        const uint32_t bg = uint32_t(std::clamp(backgroundAlpha * fade * 255.0f, 0.0f, 255.0f)) << 24;
        const uint32_t fg = (uint32_t(std::clamp(textAlpha * fade * 255.0f, 0.0f, 255.0f)) << 24) | 0xFFFFFF;
        if (fade * textAlpha > 0.02f) {
            g.fill(0, y, float(width + 8), y + 9, bg);
            g.text(line.text, 4, y + 1, fg);
        }
        ++shown;
    }
}

} // namespace mc
