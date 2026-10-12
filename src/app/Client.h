// Client.h -- what lives for the whole run: the window, the job system, assets,
// registries, options, the GUI and chat. Worlds come and go (app/WorldSession).
#pragma once

#include <memory>

#include <glm/glm.hpp>

#include "app/Cli.h"
#include "core/JobSystem.h"
#include "game/Commands.h"
#include "game/Options.h"
#include "platform/Window.h"

namespace mc {

class BiomeRegistry;
class BlockRegistry;
class Chat;
class ClientAssets;
class CreativeTabs;
class GuiGraphics;
class ItemRegistry;
class Language;

struct Client {
    CliOptions cli;
    Options options;
    Window window;
    std::unique_ptr<JobSystem> jobs;
    std::unique_ptr<ClientAssets> assets;
    std::unique_ptr<BlockRegistry> registry;
    std::unique_ptr<BiomeRegistry> biomes;
    std::unique_ptr<ItemRegistry> items;
    std::unique_ptr<Language> lang;
    std::unique_ptr<CreativeTabs> creativeTabs;
    std::unique_ptr<GuiGraphics> gui;
    std::unique_ptr<Chat> chat;
    Commands commands;
    bool screenOpen = false; // input goes to the screen, not the world
    bool chatOpen = false;   // the chat screen is up: the log shows in full
    long long chatTicks = 0; // client ticks, for chat message ages

    // GUI pixels per framebuffer pixel (the option, or vanilla's automatic choice).
    int guiScale() const;
    // The cursor in GUI pixels.
    glm::vec2 guiMouse() const;
};

} // namespace mc
