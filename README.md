# minecraft-cpp

A single-player Minecraft (Java Edition) style game written from scratch in C++20 and OpenGL 4.5.

- **Visuals:** loaded at runtime from the vanilla 1.21.7–1.21.8 resource pack in `Java Default Textures 1.21.7 - 1.21.8/`. That covers models, textures, fonts, GUI sprites and language strings.
- **Behavior data:** recipes, drops and world generation are hand-written in `data/`.
- **Sound:** none, because the pack has no audio.

Progress and the scope of each milestone are tracked in [docs/STATUS.md](docs/STATUS.md). Formulas and conventions are in [docs/DESIGN.md](docs/DESIGN.md).

## Build
You need CMake ≥ 3.20, Ninja, GCC ≥ 13 or Clang ≥ 17, and these system packages: glfw, glm, nlohmann-json, libpng, zstd and gtest. ccache is optional.

On Arch:
```sh
sudo pacman -S cmake ninja glfw glm nlohmann-json libpng zstd gtest ccache
```

Build:
```sh
cmake --preset release
cmake --build --preset release
```

Presets:
- `release`: the default build.
- `debug`: also enables a GL debug context.
- `asan`: ASan + UBSan.
- `tsan`: ThreadSanitizer.

Binaries land in `build/<preset>/bin/`.

## Run
```sh
./build/release/bin/minecraft            # windowed, 1280x720
./build/release/bin/minecraft --help     # all options
```

The game directory (saves, `options.json`, screenshots, logs, the title panorama) defaults to `./run`; change it with `--gamedir`.

The game starts at the title screen: Singleplayer leads to the world list (or straight to Create New World when there are none). The first start renders the title panorama, which takes a few seconds; `--bake-panorama` renders it again and exits.

Any option about a world opens that world directly instead (the scripted tests use this). Choose it with:
- `--world-type default|flat|debug`: `default` is the generated overworld; debug places every block state on a grid at y=70.
- `--render-distance N`
- `--seed N`
- `--pos X,Y,Z` and `--look YAW,PITCH`: the camera's start. `--pos X,~,Z` starts 12 blocks above the generated terrain.
- `--perspective first|back|front`: the starting view (F5 cycles it).

`--gallery` shows the M1 block gallery instead.

`--exec "/setblock 0 -60 0 stone"` runs commands once the spawn area is loaded. `--hold forward,sprint,attack` holds controls down for the whole run (forward, back, left, right, jump, sneak, sprint, attack, use), for scripted screenshots.

More options:
- `--time T` (0 = sunrise, 6000 = noon, 18000 = midnight)
- `--freeze-time`
- `--debug-overlay` (F3)
- `--smooth-lighting off`

Play options:
- `--gamemode survival|creative|adventure|spectator` (default survival). Spectator flies through blocks, which the screenshot tests use.
- `--world NAME`: play `<gamedir>/saves/NAME`, creating it if missing (an existing world keeps its seed and type). Without it the world is not saved. Worlds autosave every 5 minutes and on exit.
- `--play NAME`: start at the title and open `<gamedir>/saves/NAME` the way the world list does, behind the loading screen.

Controls:
- Click: capture the mouse
- Mouse: look
- WASD: walk; Space: jump (double-tap in creative to fly, then Space/Shift to rise/sink)
- Shift: sneak (never walks off ledges); Ctrl: sprint
- Left mouse: break (instant in creative, otherwise at the held tool's speed); middle mouse: pick block (creative)
- Right mouse: place; open doors, trapdoors and gates, flip levers, press buttons; open crafting tables, chests, barrels, ender chests and furnaces; sleep in a bed at night (it also sets the respawn point); write on a sign (dyes, glow ink sacs, ink sacs and honeycomb change it instead); eat and drink (hold); till with a hoe, make paths with a shovel, strip and scrape with an axe, carve pumpkins with shears, wax copper, light fires with flint and steel; put on armor. Sneak to place against a block instead of using it
- 1–9 or the mouse wheel: hotbar slot; F: swap hands; Q: drop one of the held item (Ctrl+Q: the stack)
- E: inventory (creative: the creative inventory with tabs and search). In menus: click, right click (half / one), shift-click, drag to spread, double-click to collect, 1–9 and F to swap with the hotbar or off hand, Q to throw, middle click to copy (creative)
- Esc: pause menu (Options, Save and Quit to Title); in menus, back
- T: chat; /: chat with a command started. Enter sends, Up/Down recall sent lines, Page Up/Down scroll
- F2: screenshot
- F3: debug screen (with the targeted block)
- F5: third person from behind, then from the front, then back to first person
- F11: fullscreen

Commands (in chat, or `--exec "/cmd"`): `/help`, `/list`, `/me`, `/seed`, and with "Allow Commands" (always on for command-line worlds) `/say`, `/difficulty`, `/gamemode`, `/give <item> [count]`, `/clear [item] [count]`, `/tp`, `/teleport`, `/time`, `/gamerule doDaylightCycle|keepInventory|naturalRegeneration [value]`, `/setblock x y z block[{nbt}]` (sign text as in vanilla: `oak_sign{front_text:{messages:["Hi","","",""]}}`), `/fill`, `/summon item|experience_orb [x y z] [nbt]`, `/experience add|set|query <target> [amount] [points|levels]` (and `/xp`; the target is always the player), `/kill`, `/item replace block x y z container.N with item [count]`, and `/assertblock x y z block` for test scripts (a mismatch makes the run exit with code 1).

Survival: health, hunger and saturation, air, fall and fire damage, armor, death (the inventory and experience drop; Respawn or Title Screen) and respawning at the last bed. Tools mine at their tier's speed and only the right tier drops ores; tools and armor wear out. Drops and thrown items lie on the ground until picked up; experience orbs fly to the player. Crafting works in the inventory's 2×2 grid and at crafting tables (1,063 recipes), furnaces, smokers and blast furnaces smelt with fuel, and two worn tools of a kind repair each other. The Attack Indicator option (Video Settings) shows the attack cooldown under the crosshair or beside the hotbar.

`--dump-model-sheets DIR` renders every block state into 32-px tiles and reports any missing textures. `--dump-item-icons DIR` renders every item's GUI icon into one sheet and reports empty or missing-texture icons. The GL tests use both. `--screen NAME` opens a screen for screenshots: in a world once it is ready (`inventory`, `pause`, `options`, `video`, `chat`), or over the title (`title`, `worlds`, `create`, `options`, `video`). `--splash TEXT` fixes the title's splash.

World generation tools run without a window:
- `--biome-map FILE [--map-size N] [--map-step N]` writes a shaded top-down biome map around `--pos` (default 0,0), then prints biome shares, the nearest location of each biome, and the highest point.
- `--slice FILE [--map-size N]` writes a vertical cross-section (x/y plane at `--pos` z, default the spawn) and reports how much of the underground is cave air.
- `--dump-column X,Z` prints climate, spline and density values down one column.
- `--bench-gen N` generates and lights about N chunks through the pipeline and prints chunks per second (`--threads` sets the workers).

## Tests
```sh
ctest --preset release      # unit tests, plus GL screenshot tests under xvfb-run if installed
ctest --preset unit         # unit tests only
```

The GL tests need `xorg-server-xvfb` and Python with Pillow and NumPy. They render with Mesa llvmpipe.

## Source layout
| Directory | Contents |
|---|---|
| `src/core` | logging, files, JSON, jobs, RNG, images, math |
| `src/platform` | window and input (GLFW) |
| `src/gfx` | OpenGL loader (generated by `tools/gen_gl.py`), shaders, screenshots |
| `src/resources` | resource pack, atlases, models, baking |
| `src/world` | blocks, tags, shapes, chunks, pipeline, light, meshing, biomes, world generation, saves |
| `src/entity`, `src/game` | player physics and animation, particles, entity models (block entities, the player); items, inventory, placement rules, commands |
| `src/render`, `src/ui` | world, sky and entity rendering, GUI batching and fonts |
| `src/app` | main loop, command-line harness and world tools |
| `shaders/`, `data/` | GLSL sources and gameplay data |
| `tests/unit` | GoogleTest unit tests (no GL) |
| `tests/client` | client tests without a GL context: every menu laid out at GUI scales 1–4, screen behavior |
| `tools/` | loader generator and screenshot checker |
