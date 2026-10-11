# Design reference

These are the formulas and conventions the code follows. Values marked *(mem)* come from memory of vanilla
1.21 rather than from the pack. Tune them freely and log the changes in `STATUS.md`.

## Conventions
- **Axes:**
  - x is east, z is south, y is up.
  - Yaw 0 faces +z (south) and yaw 90 faces −x (west). Positive pitch looks down.
  - Facing vector: `(-sin(yaw)·cos(pitch), -sin(pitch), cos(yaw)·cos(pitch))`.
- **Directions:**
  - Order is `DOWN, UP, NORTH(−z), SOUTH(+z), WEST(−x), EAST(+x)`, and `opposite(d) = d ^ 1`.
  - Horizontal order for rotation is N, E, S, W; `y=90` maps N→E.
  - The neighbor-update order is W, E, D, U, N, S.
- **World:** y runs from −64 to 319 (24 sections), with sea level 63. A section index is `(y<<8)|(z<<4)|x`. There are 4×4×4 biome cells per section.
- **Time:** 20 ticks per second and 24,000 ticks per day. Noon is 6000 and midnight 18000. The moon phase is `(day % 8)`.
- **Celestial angle:**
  ```
  d = frac(time/24000 - 0.25)
  e = 0.5 - cos(d·π)/2
  angle = (2d + e)/3
  ```
- **RNG:**
  - Never use `std::*_distribution`.
  - Xoroshiro128++ for worldgen and gameplay, seeded through splitmix64.
  - `JavaRandom` is the LCG with multiplier 0x5DEECE66D, addend 0xB and a 48-bit state. Its `nextInt(bound)` uses the rejection loop.
  - `Mth.getSeed(x,y,z)`:
    ```
    l = (int64)(int32)(x*3129871) ^ (int64)z*116129781 ^ (int64)y
    l = l*l*42317861 + l*11
    return l >> 16
    ```
    The `x*3129871` product wraps in 32 bits. Do all of it in unsigned arithmetic, then cast.
- **Random model variant:**
  ```
  r = JavaRandom(Mth.getSeed(pos))
  idx = weightedPick(abs((int32)r.nextLong()) % totalWeight)
  ```

## Model baking (`resources/ModelBaker`)
- **Parent merge:**
  - `textures` merge, and the child wins.
  - `elements` come from the nearest model that has any.
  - `display` merges per context.
  - `ambientocclusion` defaults to true; it and `gui_light` take the nearest value.
- **Texture references:** `#name` is resolved recursively, and the `#` is optional in face textures. A value without a namespace means `minecraft:`.
- **Face vertex order** (vanilla FaceInfo), with X0/X1 = from/to:
  ```
  DOWN  (X0,Y0,Z1)(X0,Y0,Z0)(X1,Y0,Z0)(X1,Y0,Z1)   UP    (X0,Y1,Z0)(X0,Y1,Z1)(X1,Y1,Z1)(X1,Y1,Z0)
  NORTH (X1,Y1,Z0)(X1,Y0,Z0)(X0,Y0,Z0)(X0,Y1,Z0)   SOUTH (X0,Y1,Z1)(X0,Y0,Z1)(X1,Y0,Z1)(X1,Y1,Z1)
  WEST  (X0,Y1,Z0)(X0,Y0,Z0)(X0,Y0,Z1)(X0,Y1,Z1)   EAST  (X1,Y1,Z1)(X1,Y0,Z1)(X1,Y0,Z0)(X1,Y1,Z0)
  ```
- **Auto-UV** (in pixels 0–16), when `uv` is missing:
  ```
  DOWN  [x0,16-z1,x1,16-z0]  UP    [x0,z0,x1,z1]        NORTH [16-x1,16-y1,16-x0,16-y0]
  SOUTH [x0,16-y1,x1,16-y0]  WEST  [z0,16-y1,z1,16-y0]  EAST  [16-z1,16-y1,16-z0,16-y0]
  ```
- **Vertex UV**, with face `rotation` r: `j = (i + r/90) % 4`, `u = uv[(j==0||j==1)?0:2]`, `v = uv[(j==0||j==3)?1:3]`.
- **Element rotation** (origin o, axis, angle θ):
  - `p = R_axis(θ)·(p-o)`;
  - with `rescale`, scale the two non-axis components by `1/cos θ`;
  - then `p += o`.
- **Blockstate rotation:**
  - `R = RotY(-y°)·RotX(-x°)` about (8,8,8), so x is applied first;
  - `y:90` maps N→E, and `x:90` maps U→N and N→D;
  - rotate the face direction and its cullface with it.
- **uvlock:**
  - Canonical texture axes (û, v̂) per face: N(−X,−Y), S(+X,−Y), E(−Z,−Y), W(+Z,−Y), U(+X,+Z), D(+X,−Z).
  - Per vertex: `uv' = Q·(uv-8)+8`, where
    ```
    Q = [[(Rû)·û', (Rv̂)·û'], [(Rû)·v̂', (Rv̂)·v̂']]
    ```
    û and v̂ are the original face's axes, and û′ and v̂′ belong to the rotated face.
  - Test case: a step top `[8,0,16,16]` at `y=90` becomes `[0,8,16,16]`. Side faces under a pure y rotation are the identity.
- **Directional shade:** U 1.0, D 0.5, N/S 0.8, E/W 0.6. `shade:false` gives 1.0. Item GUI lighting uses `light.glsl` instead.
- **Overlay quads** (grass side overlay): emitted after the base quads and drawn with `GL_LEQUAL`, with depth writes on.
- **Atlas UV:** `sprite.min + uv/16·sprite.size`, inset toward the sprite center by 1/1024 of a texel's span to stop bleeding.
- **Layer extrusion** (`builtin/generated`): front and back quads per layer, plus one 1-pixel side quad for each edge run of opaque texels. Layer N is drawn with `tintindex` N.

## Lighting
- **Light BFS** (per channel; opacity o is used as `max(1, o)`):
  ```
  add:    for each neighbour nb (skip if faceBlocked(p→nb)):
            n = (sky && dir==DOWN && L==15 && opacity(nb)==0) ? 15 : L - max(1, opacity(nb))
            if n > light(nb): set, push
  remove: set 0; pop (q,Lq): for nb with Ln = light(nb):
            if Ln != 0 && (Ln < Lq || (sky && DOWN && Lq==15 && Ln==15)): zero it, push to remove
            else if Ln >= Lq: queue nb for re-add
          then run add from the re-add set plus emitters
  ```
  `faceBlocked(a→b)` = `occl(a)[dir] || occl(b)[opp]`, where `occl` is the 6-bit face-occlusion mask.
- **Smooth light and AO** (vanilla `AmbientOcclusionFace`):
  - `c = quad.flush ? pos+dir : pos`. The 4 edge cells are c plus the in-plane neighbors.
  - Per cell: `L` is the packed light (0 if opaque), and `A = 0.2` if it is a full collision cube, else 1.0.
  - The corner cell is used only if not both adjacent edges block. Otherwise use the edge's value.
  - Per vertex: `ao = (A_a + A_b + A_d + A_c)/4`. Light is the per-channel mean of the four, with any 0 replaced by `L_c`.
  - Partial quads interpolate bilinearly.
  - **Flip:** if `b0+b2 < b1+b3`, rotate the vertex order by one so the diagonal joins the brighter pair.
  - Emissive blocks and `ambientocclusion:false` models use flat light: `L(pos+dir)` if flush, else `L(pos)`.
- **Lightmap:** port the pack's `shaders/core/lightmap.fsh` into a 16×16 texture rebuilt each frame. The vertex stores level×16; sample at `(L/16+0.5)/16`.
- **Fog** (port `fog.glsl`):
  ```
  fog = max(linear(sphericalDist, envStart, envEnd), linear(cylDist, rdStart, rdEnd))
  cylDist = max(len(xz), |y|)
  ```
  Render-distance fog runs over `[RD·16 - 16, RD·16]` *(approx.)*.

## Fluids
- **Heights:** level L (0 = source) gives height `(8-L)/9`, so a source is 0.889. The height is 1.0 if the same fluid is above, and a falling fluid is 8/9.
- **Corners:**
  - 1 if any adjacent column has the same fluid above;
  - otherwise a weighted mean of the heights, with weight 10 when h ≥ 0.8;
  - the diagonal counts only if a side is open.
- **Flow vector:**
  - the sum over horizontal neighbors of `dir·(own - h_nb)`;
  - a falling fluid with a solid side adds (0,−6,0);
  - normalize.
  - The top face uses the still sprite when flow.xz is 0. Otherwise it uses the flowing sprite rotated by `atan2(flow.z, flow.x) - π/2`, sampled with radius 0.25 around the sprite center.
- **Side faces:** use the flow sprite with u in [0, 0.5]. Use `water_overlay` against glass and leaves.
- **Water:** biome water color (default #3F76E4), on the translucent layer.
- **Lava:** solid layer and emissive.
- **Spread:**
  - tick delay: water 5, lava 30;
  - drop-off: water 1, lava 2;
  - slope search distance: water 4, lava 2;
  - down first;
  - a new source forms with ≥ 2 adjacent sources and solid or source below.
- **Interactions:**
  - lava source + water → obsidian;
  - flowing lava + water → cobblestone;
  - lava flowing down onto water → stone.

## Player and physics *(mem)*
- **Ground movement:**
  ```
  input *= 0.98                    (sneak ×0.3, using an item ×0.2)
  speed = 0.1·(sprint ? 1.3 : 1)·0.216/f³     f = block friction (0.6 default; ice 0.98, blue ice 0.989, slime 0.8)
  in air: speed = sprint ? 0.026 : 0.02
  vel += rotateYaw(normalize(input)·speed)
  move
  vy = (vy - 0.08)·0.98
  vxz *= f·0.91                    (0.91 in air)
  ```
- **Jumping:** `vy = 0.42·jumpFactor`. A sprint jump adds `(-sin yaw, cos yaw)·0.2`. The apex is about 1.2522 blocks.
- **Water:** acceleration 0.02, drag 0.8 (0.9 sprinting), `vy -= 0.005`, jump +0.04.
- **Lava:** drag 0.5, `vy -= 0.02`.
- **Climbing:** `|vxz| ≤ 0.15`, `vy ≥ -0.15` (0 when sneaking). A horizontal collision gives `vy = 0.2`.
- **Creative flight:** acceleration 0.05 (0.1 sprinting), vertical speed ±0.375, and vertical drag ×0.6 per tick.
- **Collision:**
  - sweep Y, then the larger of X/Z, then the other;
  - step up 0.6 when on ground and blocked horizontally;
  - sneaking on the ground can't walk off edges.
- **Dimensions:** the hitbox is 0.6×1.8 (1.5 sneaking), eye height 1.62 (1.27 sneaking), reach 4.5 (5 in creative). Walking is 4.317 m/s and sprinting 5.612 m/s.
- **Fall damage:** `ceil(fallDistance - 3)`.

## Breaking *(mem)*
- **Progress per tick:** `speed / hardness / (canHarvest ? 30 : 100)`.
  - hardness < 0 means unbreakable; 0 means instant.
- **Tool speed:**
  - 1 by default;
  - tiers: wood 2, stone 4, iron 6, diamond 8, netherite 9, gold 12;
  - shears 15 on leaves and cobweb, 5 on wool;
  - sword 15 on cobweb, 1.5 on plants and leaves.
- **Penalties:** ×0.2 underwater without Aqua Affinity; ÷5 when not on the ground.
- **Timing:** a 5-tick delay between blocks; crack stage = `floor(progress·10)`.
- **Reference break times:**
  - hand on stone: 150 ticks;
  - wooden pickaxe on stone: 23;
  - diamond pickaxe on obsidian: 188.

## Explosions
- **Rays:** cast 16³ cube-surface rays with intensity `P·(0.7+0.6r)`. Each 0.3 step subtracts `(resistance+0.3)·0.3` inside a block or fluid, plus 0.225.
- **Entities:**
  - impact = `(1 - d/(2P))·exposure`;
  - damage = `((i²+i)/2)·7·2P + 1`;
  - knockback scales with impact.
- **Drops:** blocks drop with chance 1/P.
- **Power:** TNT 4 with an 80-tick fuse; creeper 3, with a 30-tick fuse.

## Survival *(mem)*
- **Hunger:**
  - Every 4.0 exhaustion: saturation −1 if it is above 0, otherwise food −1 (except in Peaceful).
  - Regeneration (`tickTimer` = ticks since the last heal):
    - food 20, saturation > 0, hurt: every 10 ticks heal `min(sat,6)/6` and add that much exhaustion;
    - food ≥ 18 and hurt: every 80 ticks heal 1 and add 6 exhaustion;
    - food 0: every 80 ticks deal 1 damage, down to 10 HP on Easy, 1 on Normal, or death on Hard.
  - Exhaustion costs:
    - sprinting 0.1 per meter, swimming 0.01 per meter;
    - jumping 0.05, sprint-jumping 0.2;
    - attacking and taking damage 0.1 each;
    - breaking a block 0.005.
- **Armor:** damage × `(1 - min(20, max(a/5, a - 4d/(t+8)))/25)`, where a is armor, t is toughness and d is damage.
  - points (helmet/chest/legs/boots): leather 1/3/2/1, chain 2/5/4/1, iron 2/6/5/2, gold 2/5/3/1, diamond 3/8/6/3 with toughness 2;
  - durability = base × factor, with base: helmet 11, chest 16, legs 15, boots 13;
  - factor: leather 5, chain 15, iron 15, gold 7, diamond 33.
- **Tools:**
  - durability: wood 59, stone 131, iron 250, diamond 1561, gold 32.
  - damage (wood/stone/iron/diamond/gold): sword 4/5/6/7/4; axe 7/9/9/9/7; pickaxe 2/3/4/5/2; shovel 2.5/3.5/4.5/5.5/2.5.
  - attack speed: sword 1.6, pickaxe 1.2, shovel 1.0, axe 0.8–1.0, hand 4.
- **Combat:**
  - cooldown = 20/attackSpeed ticks; multiplier `0.2 + s²·0.8`;
  - a crit (falling, scale > 0.9) does ×1.5;
  - knockback is 0.4, plus 1 level when sprinting;
  - 10 ticks of invulnerability after a hit.
- **Difficulty:** mob→player damage on Easy is `min(d/2+1, d)`, on Normal `d`, on Hard `1.5d`.
- **XP to the next level:** `2L+7` (L ≤ 15), `5L−38` (L ≤ 30), `9L−158`.
- **Items and smelting:**
  - item entities have a 10-tick pickup delay (40 when thrown), despawn after 6000 ticks, and merge within 0.5 blocks;
  - smelting takes 200 ticks (100 in a smoker or blast furnace);
  - fuel: coal 1600, log or planks 300, stick 100, lava bucket 20000, coal block 16000.
- **Sleeping:**
  - allowed from time 12541 to 23458, or during a thunderstorm;
  - no monsters within 8 blocks horizontally or 5 vertically;
  - skips to the next morning.

## Mobs *(mem)*
| Mob | HP | Speed | Size | Notes |
|---|---|---|---|---|
| zombie | 20 | 0.23 | 0.6×1.95 | damage 3, armor 2, burns in sun |
| skeleton | 20 | 0.25 | 0.6×1.99 | bow every 20 (Hard) / 40 ticks |
| creeper | 20 | 0.25 | 0.6×1.7 | swells < 3 blocks, cancels > 7, fuse 30 |
| spider | 16 | 0.3 | 1.4×0.9 | damage 2, climbs walls, neutral in light |
| pig | 10 | 0.25 | 0.9×0.9 | carrot, potato, beetroot |
| cow | 10 | 0.2 | 0.9×1.4 | wheat; milk with a bucket |
| sheep | 8 | 0.23 | 0.9×1.3 | wheat; eats grass to regrow wool |
| chicken | 4 | 0.25 | 0.4×0.7 | seeds; egg every 6000–12000 ticks; slow fall |

- **Spawning:**
  - caps per 17×17 chunk area: monster 70, creature 10;
  - hostiles need block light 0 and sky light ≤ rand(0..7);
  - animals need grass below and light > 8;
  - animals mostly spawn during chunk generation.
- **Despawning:** immediate beyond 128 blocks; a 1/800 chance per tick beyond 32 blocks.
- **Breeding:** love mode lasts 600 ticks, a baby grows up in 24,000 ticks, and the parent cooldown is 6000.
- **Bow:**
  - power `p = (f² + 2f)/3` with f = ticks/20, clamped at 1;
  - arrow speed 3p, gravity 0.05, drag 0.99;
  - damage `ceil(speed·2)`, plus a crit bonus.

## Random ticks *(mem)*
- **Rate:** 3 random blocks per section per tick.
- **Grass:** spreads to dirt within (±1, −3..+1, ±1) when the light above is ≥ 9. It decays when the block above blocks light.
- **Leaves:** `distance` 1–7. Non-persistent leaves at distance 7 decay.
- **Saplings:** grow with light ≥ 9 and a 1/7 chance, going stage 0 → 1, then the tree.
- **Crops:** chance depends on farmland moisture. Wheat, carrots and potatoes have ages 0–7; beetroot 0–3.
- **Upward growth:** cactus and sugar cane reach at most 3 tall; age 0–15.

## Worldgen (vanilla-like, not seed-compatible) *(mem)*
- **Climate noises** (NormalNoise, `firstOctave` and `amplitudes`):
  - temperature −10 [1.5,0,1,0,0,0]
  - vegetation −8 [1,1,0,0,0,0]
  - continentalness −9 [1,1,2,2,2,1,1,1,1]
  - erosion −9 [1,1,0,1,1]
  - ridge −7 [1,2,1,0,0,0]
  - shift −3 [1,1,1,0]
  
  All are sampled at quart positions (x/4).
- **Peaks and valleys:** `pv = -(|(|w| - 0.6667)| - 0.3333)·3`, where w is weirdness.
- **Biome selection** (OverworldBiomeBuilder):
  - Temperature bands: [−1, −0.45, −0.15, 0.2, 0.55, 1]. Humidity bands: [−1, −0.35, −0.1, 0.1, 0.3, 1].
  - Erosion bands: [−1, −0.78, −0.375, −0.2225, 0.05, 0.45, 0.55, 1].
  - Continentalness:
    - mushroom < −1.05
    - deep ocean < −0.455
    - ocean < −0.19
    - coast < −0.11
    - near-inland < 0.03
    - mid-inland < 0.3
    - far-inland beyond
  - Middle biomes by [temperature][humidity]:
    - T0: snowy_plains ×3, snowy_taiga, taiga
    - T1: plains, plains, forest, taiga, old_growth_spruce_taiga
    - T2: flower_forest, plains, forest, birch_forest, dark_forest
    - T3: savanna, savanna, forest, jungle, jungle
    - T4: desert ×5
  - Variants:
    - T0: ice_spikes at H0, snowy_taiga at H2
    - T1: old_growth_pine_taiga at H4
    - T2: sunflower_plains at H0, old_growth_birch_forest at H3
    - T3: plains at H2, sparse_jungle at H3, bamboo_jungle at H4
  - Plateau biomes:
    - T0: snowy_plains ×3, snowy_taiga ×2
    - T1: meadow ×2, forest, taiga, old_growth_spruce_taiga
    - T2: meadow ×4, dark_forest
    - T3: savanna_plateau ×2, forest ×2, jungle
    - T4: badlands ×3, wooded_badlands ×2
  - Plateau variants:
    - T0: ice_spikes at H0
    - T1: cherry_grove at H0, meadow at H2 and H3, old_growth_pine_taiga at H4
    - T2: cherry_grove at H0 and H1, forest at H2, birch_forest at H3
    - T4: eroded_badlands at H0 and H1
  - Shattered (windswept): gravelly hills ×2, hills, forest ×2 for T0 and T1; hills ×3, forest ×2 for T2.
  - Peaks: jagged_peaks (cold), frozen_peaks, stony_peaks (T4). Slopes: snowy_slopes, grove. Plus beach, snowy_beach, stony_shore, river, frozen_river, swamp, and the oceans by temperature (frozen, cold, normal, lukewarm, warm, with deep variants).
- **Terrain** (`world/gen/OverworldGenerator`):
  - depth = `yClampedGradient(y, −64, 320, 1.5, −1.5) + offset`, where offset = spline − 0.50375;
  - jagged = `jaggedness · halfNegative(jaggedNoise(x·1500, z·1500))`;
  - base = `4 · quarterNegative((depth + jagged) · factor)`;
  - sloped = base + BlendedNoise(0.25, 0.125, 80, 160, 8);
  - sloped < 1.5625 → `min(sloped, 5·entrances)`, otherwise `underground(sloped)`;
  - slides: lerp toward −0.078125 from y 240 to 256, and toward 0.1171875 from y −40 down to −64;
  - final = min(interpolated density, noodle), solid where > 0.
  
  Density is sampled on 4×8×4 cell corners and interpolated trilinearly. The noodle inputs (selector, thickness, ridge A, ridge B) are interpolated separately, then combined per block. A deepslate gradient runs from y 0 to 8. Bedrock is a random floor from y −64 to −60.
- **Caves:**
  - underground = `min(min(4·layer² + clamp(0.27 + cheese, −1, 1) + clamp(1.5 − 0.64·sloped, 0, 0.5), entrances), spaghetti2d + roughness)`;
  - entrances = `min(caveEntrance(0.75, 0.5) + 0.37 + yGradient(−10, 30, 0.3, 0), roughness + spaghetti3d)`;
  - spaghetti 3D and 2D use the rarity-scaled samplers (TYPE1 and TYPE2).
  - Carvers: worm caves (15%, y −56..180), extra underground caves (7%, y −56..47) and canyons (1%, y 10..67), from start chunks within 8. Each carves ellipsoids along a wandering path; caves use the floor cut-off, canyons use per-height width factors.
  - About 10–13% of the volume between y −56 and 40 is cave air (`--slice` reports it).
- **Fluids and aquifers:** see the M4 deviations in `docs/STATUS.md`. The flood line is `estimate − 6`, with a 3-block stone barrier below it; lava fills air below y −54.
- **Cave biomes:** vanilla nearest-point rule. Surface biomes sit at depth 0 and 1, and lush caves (humidity ≥ 0.7) and dripstone caves (continentalness ≥ 0.8) span depth 0.2..0.9. A cave biome wins when its squared parameter distance is below `min(depth², (1 − depth)²)`.
- **Decoration (DECORATE and MERGE):**
  - Data: `data/worldgen/features/*.json` define features in vanilla's JSON shape (`type`, `config`, `placement`); `data/worldgen/biome_features.json` names the decoration steps and per-biome feature sets (with `include`); `biomes.json` picks a set per biome.
  - Within a step every feature has one global index, a stable topological merge of all sets' lists. A chunk runs, step by step in that order, each feature that any biome in the chunk has; the `biome` placement filter keeps positions whose own biome has it. Each (chunk, step, feature) gets its own seeded Xoroshiro.
  - Placement chains run depth first, like vanilla's lazy streams: later positions see earlier placements.
  - DECORATE reads the TERRAIN snapshots of the 3×3 chunks plus the source chunk's own earlier writes, and may write anywhere in the 3×3. Every write records the block it expected and its feature index.
  - MERGE sorts writes by (step, feature, source, sequence) — so all sources' trees land before any source's flowers — and applies each write if the block is unchanged; tree writes may also replace air, leaves and `replaceable_by_trees` (vanilla `validTreePos`); forced writes always apply.
  - Heightmaps (WORLD_SURFACE_WG, OCEAN_FLOOR_WG, MOTION_BLOCKING, MOTION_BLOCKING_NO_LEAVES) are cached per column and kept current by writes. "Blocks motion" comes from the `collision` and `solid` rules in `data/blocks/properties.json` until M6 adds shapes.
  - Feature code: `world/gen/feature/` — `BasicFeatures` (ore, disk, simple_block, random_patch, selectors, block_column), `Trees`/`TreePlacers`/`TreeDecorators` (8 trunk placers, 11 foliage placers, vines, cocoa, bee nests, podzol, leaf litter, huge mushrooms, fallen trees, root systems), `Vegetation*` (seagrass, kelp, sea pickles, coral, bamboo, vines, icebergs, blue ice, ice spikes, forest rocks) and `Cave*` (lakes, springs, glow lichen, underwater magma, vegetation patches, dripstone). `--check-data` reports any definition that does not resolve.
  - After MERGE, `finalizeChunk` runs vanilla's freeze_top_layer: at the MOTION_BLOCKING top, still water freezes to ice and air gets a snow layer (snowy grass below) where the height-adjusted biome temperature is below 0.15. Temperature falls by `(noise·8 + y − 80)·0.05/40` above y 80; frozen oceans thaw in noise patches.
- **Colormap lookup:** `x = (int)((1-T)·255)`, `y = (int)((1-D·T)·255)` with T and D clamped to [0,1]. Fixed colors: birch #80A755, spruce #619961, lily pad #208030.

## UI
- **GUI scale:** the largest s ≤ max such that `w ≥ 320s` and `h ≥ 240s`, which gives 4 at 1920×1080.
- **Fonts:**
  - `font/default.json` is the provider order: space, then bitmap fonts (`nonlatin_european`, `accented`, `ascii`).
  - Glyph width is the rightmost opaque column + 1, and the advance is width + 1. Space advances 4.
  - The shadow is offset (1,1) at 25% brightness.
- **Containers:** chest screens draw `generic_54.png`: the top `rows·18+17` px, then the bottom 96 px from v=126.
