# Status

The plan is `~/.claude/plans/make-minecraft-in-c-sparkling-ladybug.md`, covering the core game in milestones M0–M13. Each milestone ends runnable with its tests passing, and gets a local commit.

## Milestones
- [x] M0 Skeleton: CMake/presets, GL loader, window/input, fixed-tick loop, jobs, PNG, screenshots, CLI harness
- [ ] M1 Block models: pack, atlases, model baking, blockstates, gallery, model sheets
- [ ] M2 Chunks & meshing: registry, sections/chunks, pipeline, mesher, MDI renderer, commands for `--exec`
- [ ] M3 Light & sky: light engine, smooth light/AO, lightmap, fog, sky, day/night, F3
- [ ] M4 Terrain: climate, density, caves, aquifers, surface, biomes, tags, fluid meshing, translucent sorting, spawn
- [ ] M5 Decoration: DECORATE/MERGE, ores, trees, vegetation, snow/ice, tints/blending
- [ ] M6 Player: physics, modes, breaking, placing, shape updates, items core, ticks, saves
- [ ] M7 Entity models: ModelPart, block-entity renderers, player model, F5
- [ ] M8 GUI core: GUI, fonts, HUD, item rendering, Menu/Slot, inventories
- [ ] M9 Menus: title/panorama, world list/create, options, pause, chat, commands
- [ ] M10 Survival: tools, food, armor, loot, item entities, XP, health, crafting, furnaces, containers, beds, signs
- [ ] M11 Simulation: random ticks, fluids, falling blocks, TNT, fire, farming, weather, particles
- [ ] M12 Mobs: models, AI, A*, spawning, combat, breeding
- [ ] M13 Polish: cave culling, overlays, options, rebinding, benchmark, end-to-end script

## Stubs: render and place, but no behavior
| Area | Behavior here |
|---|---|
| Redstone components (dust, torches, repeaters, comparators, pistons, observers, hoppers, droppers, dispensers, rails, plates, tripwire) | Decorative. `powered` is always false. Levers and buttons only toggle their own state; buttons pop back after 20 or 30 ticks |
| Iron doors and iron trapdoors | Can't be opened by hand (vanilla) |
| TNT | Lit only by fire, flint and steel, or explosions |
| Rails | Place straight; no curve logic |
| Food status effects (rotten flesh, raw chicken, spider eye, golden apples, …) | Nutrition only; milk drinking is a no-op |
| Enchantments | None: no Silk Touch or Fortune drops, no glint; the creative inventory has one enchanted book |
| Data-component variants (potions, tipped arrows, fireworks, banner patterns, goat horns) | One default entry each in the creative inventory |
| Armor trims | The item-definition trim cases are never selected or baked |
| Entity-spawning items (boats, minecarts, item frames, paintings, armor stands, snowballs, eggs, ender pearls) | Do nothing. Only the 8 in-scope spawn eggs work |
| Shields and tridents | Render only |
| Job-site and other blocks (stonecutter, smithing table, loom, grindstone, cartography table, anvil, enchanting table, brewing stand, lectern, composter, cauldrons, note block, jukebox, beacon, conduit) | No menu or interaction |
| Biomes: deep dark, pale garden, mangrove swamp | Not generated; neighbors take their place |
| Powder snow | Generates as snow blocks |
| Mob variants: cold pig, cow and chicken, warm cow | Temperate model and texture are used instead |
| Dragon and piglin heads | Plain-cube stand-ins |
| Sound | None (the pack has no audio) |

## Deviations from vanilla
(Recorded per milestone.)
