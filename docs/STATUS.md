# Status

The plan is `~/.claude/plans/make-minecraft-in-c-sparkling-ladybug.md`, covering the core game in milestones M0–M13. Each milestone ends runnable with its tests passing, and gets a local commit.

## Milestones
- [x] M0 Skeleton: CMake/presets, GL loader, window/input, fixed-tick loop, jobs, PNG, screenshots, CLI harness
- [x] M1 Block models: pack, atlases, model baking, blockstates, gallery, model sheets
- [x] M2 Chunks & meshing: registry, sections/chunks, pipeline, mesher, MDI renderer, commands for `--exec`
- [x] M3 Light & sky: light engine, smooth light/AO, lightmap, fog, sky, day/night, F3
- [x] M4 Terrain: climate, density, caves, aquifers, surface, biomes, tags, fluid meshing, translucent sorting, spawn
- [x] M5 Decoration: DECORATE/MERGE, ores, trees, vegetation, snow/ice, tints/blending
- [x] M6 Player: physics, modes, breaking, placing, shape updates, items core, ticks, saves
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
Recorded per milestone.
- **M1:**
  - Random model variants use the 1.21.5+ rule: `JavaRandom(Mth.getSeed(pos)).nextInt(totalWeight)`. Multipart parts restart from one derived seed. Older versions used `abs((int)nextLong()) % total`.
  - Atlas UVs are pulled 0.1% toward the opposite corner, as in pre-1.21 FaceBakery.
  - All block-atlas sprites are aligned to 16 texels, so the atlas keeps 4 mip levels. The 8×8 conduit eyes would otherwise cap it at 3.
  - `paletted_permutations` atlas sources (armor trims) are skipped.
- **M2:**
  - Block states come from behavior-class schemas (`data/blocks/classes.json`, with schemas in `BlockRegistry.cpp`), or are inferred from blockstate files for blocks without behavior. That gives 25,986 states; vanilla has about 27k, because inferred blocks lack non-visual properties such as note block instruments.
  - Face culling uses a 4×4 coverage grid per face, built from fully opaque flush quads, instead of vanilla's VoxelShape occlusion test.
  - Outline and collision shapes are deferred to M6, where they are first used.
- **M3:**
  - Quads are split along the diagonal joining their brighter corners (anisotropy fix, as in Sodium). Vanilla always splits 0-2.
  - Partial quads interpolate the four AO corner values bilinearly. This is equivalent to vanilla's `SizeInfo` weight tables.
  - The lightmap is sampled at texel centers, so integer light levels are exact. Vanilla samples at uv/256, which blends neighboring levels.
  - Fog: render-distance fog covers the last `clamp(rd/10, 4, 64)` blocks; environmental fog appears only in rain. Vanilla's exact atmospheric fog constants are not reproduced.
  - Clouds are rebuilt every frame within 16 cells (192 blocks), at a height of 192.33. They fade with distance.
  - The incremental light removal ignores face occlusion: it may over-remove, and re-adding repairs it. A test checks it against from-scratch light after 10,000 random edits.
- **M4:**
  - Noise, splines and density follow vanilla's algorithms (ImprovedNoise, PerlinNoise, NormalNoise, BlendedNoise, CubicSpline, the TerrainProvider splines and the overworld noise router). Every noise is seeded from our own Xoroshiro name hashing, so worlds are not seed-compatible with vanilla.
  - Noise caves have no `pillars` term. Only the sign of the final density is used, so the 0.64 multiplier and `squeeze` are skipped.
  - Aquifers are a stand-in for vanilla's noise-based aquifer grid. Below sea level, air turns to water down to a flood line 6 blocks under the spline surface; dry air in the 3 blocks under that line becomes stone (a barrier); deeper caves stay dry, and lava fills air below y −54. There are no randomly flooded deep aquifers.
  - Carvers have vanilla's structure and parameters: `cave` (15%), `cave_extra_underground` (7%, y −56..47) and `canyon` (1%), drawn from our own random streams. Instead of consulting an aquifer, carvers never open a block that touches water.
  - Surface biomes come from threshold tables equivalent to `OverworldBiomeBuilder`'s parameter list. Cave biomes use vanilla's nearest-point rule against the surface biomes' depth.
  - Surface rules are a hand-written approximation of vanilla's rule tree: grass/dirt, sand/sandstone, ocean gravel, river beds, snowy peaks and slopes, gravelly hills, badlands terracotta bands, calcite, podzol, coarse dirt, mycelium and swamp clay.
  - Spawn: the nearest land column on a square spiral of 16-block steps that is not ocean, river, beach or peaks. Vanilla searches climate parameter space instead. The free camera starts 12 blocks above it until the player arrives in M6.
  - The biome statistics test samples 32768² blocks per seed, not 4096². The continentalness noise varies over about 2,000 blocks, so 4096² samples range from 16% to 53% ocean. Over large areas ocean is 29–31%.
  - Fluid side faces are hidden only by a neighbor whose face is fully covered. Vanilla also compares the fluid's height with the neighbor's shape.
- **M5:**
  - Feature data (`data/worldgen/features/*.json`, `biome_features.json`) reproduces vanilla's configured and placed features from memory; there are no vanilla data files. The 1.21.5 additions (bushes, dry grass, leaf litter, wildflowers, firefly bushes, fallen trees, cactus flowers) are the least certain.
  - Features decide against the TERRAIN snapshots of their 3×3 area plus their own source's writes, so features from neighboring source chunks cannot see each other. MERGE applies writes feature by feature and skips stale ones; a post-pass removes column-supported blocks (plants, dripstone, carpets) whose support another source replaced. Overlapping trees from different sources can keep leaf distances that are too large (about 3% of leaves); leaf decay arrives in M11.
  - Not generated: structures (monster rooms, geodes, fossils, desert wells), mangrove swamps, pale gardens and the deep dark (as planned). Bee nests have no bees. Springs stay single source blocks until fluids tick (M11).
  - Underwater magma almost never appears: it needs flooded caves at least two blocks below the sea floor, which the aquifer stand-in rarely makes.
  - Noise-driven counts and states (flower patches, coral, kelp, bamboo, meadow flowers) use stand-in noises, and geometry uses `std::sin`/`std::cos` rather than vanilla's lookup tables. Worlds are not seed-compatible anyway.
  - Ice spikes and groves get snow-block surfaces (vanilla mixes in powder snow). Snow and ice ignore block light, which does not exist yet at generation time. Mushrooms survive regardless of light during generation.
  - Every top-level random call is sequenced: C++ leaves the order of operands such as `nextInt() - nextInt()` unspecified, and worlds must not depend on the compiler.
- **M6:**
  - Shapes: outlines and collision come from model element boxes (element rotations ignored), with vanilla overrides for fences, panes, walls, gates, snow layers, cactus, soul sand, mud, honey, lily pads, ladders, torches and the code-rendered blocks (chests, beds, signs, banners, heads, shulker boxes, pot, conduit). Flat models (flowers, vines) outline their quads' bounds. Vanilla's hand-written shapes differ in detail (for example flower outlines are smaller).
  - Step-up uses the pre-1.21 "step up, then across" search; 1.21's candidate step heights give the same result for blocks.
  - Placement covers the vanilla rules for stairs, slabs (with doubling), doors (approximate hinge choice), beds, double plants, trapdoors, buttons, levers, grindstones, torches, signs, banners, heads, ladders, rails (straight), chests (pairing), hoppers, pistons, observers, dispensers, barrels, anvils, lanterns, rods and pillars; other blocks fall back to their `facing`/`axis`/`rotation` properties. Wall `tall` sides follow only whether the block above has a sturdy bottom face.
  - Saves keep only edited chunks: every chunk still generates (so neighbors receive its tree and plant writes), and a saved chunk's blocks replace the generated ones before lighting. Light is not saved. `/assertblock` stands in for the planned `--dump-world-state` diff.
  - Particles: block break and mining debris only (vanilla TerrainParticle); other particle types come with the simulation milestone.
  - The hotbar shows slot counts and the selected item's name; item icons arrive with the GUI milestone. Creative starts with a few building blocks because the creative inventory comes later.
