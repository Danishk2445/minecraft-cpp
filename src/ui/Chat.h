// Chat.h -- the chat log (vanilla ChatComponent): wrapped messages above the
// hotbar that fade after 10 seconds unless chat is open, and the history of
// sent lines for the up and down keys.
#pragma once

#include <deque>
#include <string>
#include <vector>

namespace mc {

class GuiGraphics;
struct Options;

class Chat {
public:
    static constexpr int kMaxMessages = 100;

    // A message (§ codes allowed) at game tick `tick`.
    void addMessage(std::string text, long long tick);
    // Remembers a line the player sent (commands included).
    void addSent(const std::string& line);
    const std::vector<std::string>& sent() const { return sent_; }
    void clear() { messages_.clear(); }
    const std::deque<std::string>& messages() const { return text_; }

    // Draws the log: recent lines fading out, or (open) the last 20 with scrolling.
    void render(GuiGraphics& g, const Options& options, long long tick, bool open);
    void scroll(int lines);
    void resetScroll() { scroll_ = 0; }

private:
    struct Line {
        std::string text; // one wrapped line
        long long tick;
    };
    std::deque<Line> messages_; // wrapped, newest first
    std::deque<std::string> text_;
    std::vector<std::string> sent_;
    int scroll_ = 0;
    int wrapWidth_ = 320;
    GuiGraphics* lastGraphics_ = nullptr;
};

} // namespace mc
