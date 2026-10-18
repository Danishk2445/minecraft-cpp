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

- **Random block offset** (vanilla OffsetType; `offset`, `max_horizontal_offset`, `offset_shapes` in `data/blocks/properties.json`):
  ```
  l = Mth.getSeed(x, 0, z)          // one offset per column: both halves of a tall plant agree
  f(k) = (float)((l >> k) & 15) / 15.0f
  dx = clamp((f(0) - 0.5)·0.5, ±maxH)   dz = clamp((f(8) - 0.5)·0.5, ±maxH)   // maxH 0.25, bamboo 0.45, dripstone 0.125
  dy = XYZ ? (f(4) - 1)·maxV : 0        // maxV 0.2: grass, ferns, dry grass and small dripleaf sink
  ```
  The model moves; its lighting stays the cell's. Outlines and collision move only for flowers, bamboo, pointed dripstone, propagules and small dripleaf.

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
- **Ticks (`world/sim/Fluids`):** four fluid tick types (water, flowing water, lava, flowing lava), one pending tick per position and type; a tick runs only if the cell still holds that type. Fluids in freshly generated chunks that can flow out get a tick when the chunk starts ticking (vanilla post-processing).
- **Currents:** entities in a fluid are pushed along its flow vector, scaled by `0.014` (water) or `0.0023333` (lava); the push is normalized except for players, and shrunk when shallower than 0.4.
- **Buckets:** an empty bucket picks up a source (ray with SOURCE_ONLY) or a waterlogged block's water; a full one fills a waterloggable block or replaces what water washes away.

## Player and physics
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
- **Implementation** (`entity/Player`, `entity/Physics`): unit tests pin the jump apex (1.2522), walk, sprint and sneak speeds, slab stepping, ledge guarding, ladder climbing (0.1176 blocks per tick), creative flight (10.92 m/s) and swimming. Collision shapes come from model elements with vanilla overrides (`world/block/BlockShapes.cpp`): fence and wall collision is 1.5 tall, snow layers collide one layer lower, cactus, soul sand, mud and honey are inset.
- **Fall damage:** `ceil(fallDistance - 3)`.

## Entity models (`entity/model`, `render/EntityRenderer`)
- **ModelPart:** parts hold boxes (`addBox(x, y, z, w, h, d)` in model units of 1/16 block, y pointing down) and a pose. A part's matrix is `T(offset/16)·Rz·Ry·Rx·S(scale)` on top of its parent's. `grow` inflates a box but not its UVs; `mirror` swaps the box's x sides in the texture.
- **Box UV** for a w×h×d box at texOffs (u0, v0):
  ```
  u: l = u0, m = l + d, n = m + w, o = n + w, p = n + d, q = p + w
  v: r = v0, s = r + d, t = s + h
  down (m,r)-(n,s)   up (n,s)-(o,r)     west (l,s)-(m,t)
  north (m,s)-(n,t)  east (n,s)-(p,t)   south (p,s)-(q,t)
  ```
- **Entity lighting:** two world-space lights, `(0.2, 1, −0.7)` and `(−0.2, 1, 0.7)` normalized; `shade = min(1, 0.4 + 0.6·Σ max(0, n·l))`. Models are also multiplied by the lightmap at the entity's eye block.
- **Block entities** (chests, beds, signs, banners, heads, shulker boxes, the bell, the pot, the conduit) are baked per block state into the chunk mesh, in the cutout layer. They take the cell's flat light, and their textures go in the block atlas. Transforms into the block:

  | Kind | Transform (yRot: south 0, west 90, north 180, east 270) |
  |---|---|
  | chest | `T(.5)·Ry(−yRot)·T(−.5)` (the chest layers are y-up) |
  | bed | `T(0, .5625, 0)·Rx(90°)·T(.5)·Rz(180 + yRot)·T(−.5)` |
  | sign | `T(.5)·Ry(−deg)·[wall: T(0, −.3125, −.4375)]·S(2/3, −2/3, −2/3)`, deg = rotation·22.5 or yRot |
  | hanging sign | `T(.5, .9375, .5)·Ry(−deg)·T(0, −.3125, 0)·S(1, −1, −1)`, drawn from both sides |
  | banner | `T(.5, 0, .5)·Ry(−deg)·S(2/3, −2/3, −2/3)`; the flag is tinted by the dye color |
  | head | floor `T(.5, 0, .5)·S(−1, −1, 1)·Ry(rotation·22.5)`; wall `T(.5 − .25·dx, .25, .5 − .25·dz)·S(−1, −1, 1)·Ry(yRot(opposite facing))` |
  | shulker box | `T(.5)·S(.9995)·R(facing)·S(1, −1, −1)·T(0, −1, 0)` |
  | decorated pot | `T(.5, 0, .5)·Ry(180 − yRot)·T(−.5, 0, −.5)` |

  Placement rotations: signs and banners use `segment(yaw + 180)`, heads `segment(yaw)`. Shulker boxes face the clicked face, and pots face the player's horizontal direction.
- **Living entities** are placed with `T(feet + offset)·Ry(180 − bodyYaw)·S(−s, −s, s)·T(0, −1.501, 0)`. The player has s = 0.9375 and drops 2/16 when crouching. Its model is 1.875 blocks tall, against a 1.8-block hitbox.
- **Walk animation (per tick):**
  ```
  speed += (min(4·dist, 1) − speed)·0.4     dist = horizontal movement this tick
  position += speed
  ```
  When rendering, `speed(pt) = lerp(speedOld, speed, pt)` and `position(pt) = position − speed·(1 − pt)`.
- **Body yaw (per tick):**
  - The target is the movement yaw when the entity moved more than 0.05 blocks. When walking backwards (`|yaw − moveYaw|` in 95..265) it is the movement yaw minus 180. While swinging, it is the look yaw.
  - `body += wrap(target − body)·0.3`, then the body is kept within 50° of the head.
  - The head yaw follows the look every tick.
- **Arm swing:** a swing lasts 6 ticks and restarts only once half done. `attackAnim = swingTime/6`; the interpolation runs on to 1 when the swing wraps to 0.
- **HumanoidModel.setupAnim** (p = walk position, a = walk speed, angles in radians):
  ```
  head.xRot = pitch, head.yRot = headYaw − bodyYaw
  rightArm.xRot = cos(0.6662p + π)·a        leftArm.xRot = cos(0.6662p)·a
  rightLeg.xRot = cos(0.6662p)·1.4a         leftLeg.xRot = cos(0.6662p + π)·1.4a
  legs: yRot ±0.005, zRot ±0.005
  holding an item:  arm.xRot = arm.xRot·0.5 − π/10
  swing t > 0:      body.yRot = sin(√t·2π)·0.2 (negated for the left arm)
                    right arm at x = −cos(body.yRot)·5, z = sin(body.yRot)·5 (left arm mirrored)
                    both arms yRot += body.yRot, left arm xRot += body.yRot
                    arm.xRot −= sin((1 − (1 − t)⁴)·π)·1.2 + sin(tπ)·(0.7 − head.xRot)·0.75
                    arm.yRot += 2·body.yRot, arm.zRot −= 0.4·sin(tπ)
  crouching:        body.xRot = 0.5, arms.xRot += 0.4, legs.z += 4, head.y += 4.2, body.y and arms.y += 3.2
  idle sway:        right arm zRot += cos(0.09·age)·0.05 + 0.05, xRot += sin(0.067·age)·0.05 (left arm negated)
  ```
- **Third person (F5)** cycles first person, behind, and in front:
  - The camera starts at the eyes. The eye height eases toward its target by `h += (target − h)·0.5` per tick.
  - The front view adds 180° to the yaw and negates the pitch.
  - The camera then backs away along −forward by `getMaxZoom(4)`. Eight rays leave from the eye ± 0.1 on each axis, each as long as the zoom found so far. They stop at visual shapes: collision shapes, except that glass and panes have none. A hit shortens the zoom to its distance from the eye.
  - Targeting always uses the player's eyes and look direction.
- **Shadows:**
  - `strength = 1 − d²/256`, with d the distance to the camera.
  - Shadows cover the blocks within `radius` of the feet (0.5 for the player), for y from `feetY − min(2·strength, radius)` up to `feetY`.
  - A shadow quad lies on visible, full-collision blocks whose raw light `max(block, sky − skyDarken)` is above 3.
  - Each quad's alpha is `(strength − (feetY − y)·0.5)·0.5·b/(4 − 3b)`, with b = raw light/15.
  - `misc/shadow.png` spans 2·radius.
  - `skyDarken = (int)((1 − (0.5 + 2·clamp(cos 2π·angle, −¼, ¼))·(1 − rain·5/16)·(1 − thunder·5/16))·11)`.

## Breaking
- **Progress per tick:** `speed / hardness / (canHarvest ? 30 : 100)`.
  - hardness < 0 means unbreakable; 0 means instant.
- **Tool speed:**
  - 1 by default;
  - tiers: wood 2, stone 4, iron 6, diamond 8, netherite 9, gold 12;
  - shears 15 on leaves and cobweb, 5 on wool;
  - sword 15 on cobweb, 1.5 on plants and leaves.
- **Penalties:** ×0.2 underwater without Aqua Affinity; ÷5 when not on the ground.
- **Timing:** a 5-tick delay between blocks; crack stage = `floor(progress·10)`.
- **Data:** `hardness` and `requires_tool` rules in `data/blocks/properties.json`; tool tiers and kinds in `data/items/items.json`.
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
- **Drops:** TNT drops everything (no decay since 1.21); creepers' blasts keep each drop with chance 1/P. Loot merges into stacks of up to 16, popped at the first block. TNT caught in a blast is lit on a fuse of `rand(20) + 10` ticks.
- **Exposure:** the share of points on the entity's box, a grid `1/(2·size+1)` apart, with a clear line (collision shapes) to the center.
- **Power:** TNT 4 with an 80-tick fuse, exploding from 1/16 of its height; creeper 3, with a 30-tick fuse.
- **Primed TNT:** hops `(−sin a·0.02, 0.2, −cos a·0.02)`; gravity 0.04, drag 0.98, ground bounce ×(0.7, −0.5, 0.7). It swells by `1 + g⁴·0.3` (`g = 1 − fuse/10`) over its last 10 ticks and flashes white when `⌊fuse⌋/5` is even.

## Survival (`entity/Player`, `game/FoodData`, `game/Damage`, `game/Tools`)
- **Hunger** (FoodData):
  - Every 4.0 exhaustion: saturation −1 if it is above 0, otherwise food −1 (except in Peaceful).
  - Regeneration (`tickTimer` = ticks since the last heal):
    - food 20, saturation > 0, hurt: every 10 ticks heal `min(sat,6)/6` and add that much exhaustion;
    - food ≥ 18 and hurt: every 80 ticks heal 1 and add 6 exhaustion;
    - food 0: every 80 ticks deal 1 damage, down to 10 HP on Easy, 1 on Normal, or death on Hard.
  - Peaceful with naturalRegeneration: heal 1 every 20 ticks and add 1 food every 10.
  - Exhaustion costs:
    - sprinting 0.1 per meter, swimming 0.01 per meter (meters rounded to centimeters);
    - jumping 0.05, sprint-jumping 0.2;
    - taking damage 0.1 (most sources), attacking 0.1 (M12);
    - breaking a block 0.005.
  - Eating (FoodData.add): `food = min(food + n, 20)`, `saturation = min(saturation + s, food)`, where s is the food's saturation (nutrition × modifier × 2 in the older terms: a cake slice is 2 and 0.4). Sprinting needs food > 6.
- **Damage** (hurt, actuallyHurt):
  - A hit starts 20 ticks of invulnerability. While more than 10 remain, only a harder hit counts, and only by what it adds over the last.
  - Armor wears by `max(1, ⌊d/4⌋)` per piece, then damage × `(1 − clamp(a − d/(2 + t/4), a/5, 20)/25)`, where a is armor, t toughness and d damage.
  - Falling (1.21.5): `⌊(fall + 1e−6 − 3)·m⌋`, with the landing tick's movement counted. Blocks' fallOn: hay and honey m = 0.2; slime 0 unless sneaking; beds halve the fall; a stalagmite tip adds 2.5 and m = 2.
  - Drowning: air 300, −1 per tick with the eyes in water; at −20 it resets to 0 and deals 2. It refills 4 per tick.
  - Lava: 4 and 15 s of fire. Fire blocks: 1 (soul fire 2) and 8 s of fire. Burning: 1 every 20 ticks. Cactus, campfires, magma (not sneaking), sweet berry bushes (while moving) and suffocation: 1 each. The void (64 below the bottom): 4.
- **Death:** experience dropped is `min(7·level, 100)`; the inventory spills (unless keepInventory); the camera rolls `40 − 8000/(min(deathTime, 20) + 200)` degrees, and the model falls over `min(√((deathTime − 1)/20·1.6), 1)·90` degrees about z.
- **Hurt overlay:** while hurt or dead the model mixes 30% red (OverlayTexture: red at alpha 178, `mix(color, red, 1 − 178/255)`) before the light.
- **Armor:**
  - points (helmet/chest/legs/boots): leather 1/3/2/1, chain 2/5/4/1, iron 2/6/5/2, gold 2/5/3/1, diamond 3/8/6/3 and netherite 3/8/6/3, toughness 2 for diamond and 3 for netherite;
  - durability = base × factor, with base: helmet 11, chest 16, legs 15, boots 13;
  - factor: leather 5, chain 15, iron 15, gold 7, diamond 33, netherite 37.
  - Right-clicking a piece swaps it with what is worn (Equippable.swapWithEquipmentSlot).
- **Tools** (Tool component):
  - durability: wood 59, stone 131, iron 250, diamond 1561, gold 32, netherite 2031.
  - damage (wood/stone/iron/diamond/gold): sword 4/5/6/7/4; axe 7/9/9/9/7; pickaxe 2/3/4/5/2; shovel 2.5/3.5/4.5/5.5/2.5.
  - attack speed: sword 1.6, pickaxe 1.2, shovel 1.0, axe 0.8–1.0, hoe 1–4, hand 4.
  - Wear: 1 per block with hardness > 0 (swords 2). Correct-tool rules: `minecraft:incorrect_for_<tier>_tool` denies drops first, then `mineable/<kind>` gives the tier's speed and drops.
- **Attack strength:** `attackStrengthTicker` counts up each tick; it resets when swinging at nothing, attacking, or changing the kind of held item. Scale `clamp((ticker + adjust)/(20/attackSpeed), 0, 1)`; attack damage × `0.2 + s²·0.8` (M12). A miss outside creative pauses attacking for 10 ticks (missTime).
- **Combat** (Player.attack, `game/Combat`):
  - damage = the weapon's attack damage × `0.2 + s²·0.8`, s the attack strength at +0.5 ticks; the ticker resets;
  - critical (s > 0.9, falling, not on the ground, a ladder, in water or sprinting): ×1.5;
  - every hit pushes 0.4 away from where it came from (vanilla `knockback`: `v/2 − dir·k`, upward `min(0.4, vy/2 + k)` on the ground, less the knockback resistance); a full-strength sprinting hit adds 0.5 along the look and slows the attacker to ×0.6;
  - sweep (a sword, s > 0.9, no crit or sprint, on the ground, moved less than 2.5 × the walk speed this tick): everything within the target's box grown by (1, 0.25, 1) and 3 blocks of the player takes `1·s` and a 0.4 push;
  - weapons wear 1 (swords) or 2 (tools); attacking costs 0.1 exhaustion; damage over 2 throws `⌊d/2⌋` damage indicators.
- **Difficulty:** damage to players that scales (mob attacks and arrows from mobs, explosions) is `min(d/2+1, d)` on Easy, `d` on Normal, `1.5d` on Hard, nothing on Peaceful. Regional difficulty (equipment, burning zombies' fire): `difficulty·(0.75 + clamp((t − 72000)/1440000, 0, 1)·0.25 + moon)`, the special multiplier `clamp((e − 2)/2, 0, 1)`.
- **Experience:** to the next level `2L+7` (L ≤ 15), `5L−38` (L ≤ 30), `9L−158`. Orbs split into the largest of 2477, 1237, 617, 307, 149, 73, 37, 17, 7, 3, 1; they drift to a player within 8 blocks (`(1 − d/8)²·0.1` per tick) and are taken one per 2 ticks.
- **Item entities:** a 10-tick pickup delay (40 when thrown); gravity 0.04 with 0.98 drag (×0.6 friction on the ground); they rise slowly in water; merge with equal stacks within 0.5 blocks; despawn after 6000 ticks; burn in fire and lava unless fire resistant.
- **Furnaces** (AbstractFurnaceBlockEntity.serverTick): fuel lights when there is something to cook; cooking takes the recipe's time (200; 100 in a smoker or blast furnace); without fire the progress unwinds 2 per tick. Experience gathers per item and is paid when the output is taken, its fraction rounded up by chance. Fuel: coal 1600, logs and planks 300, sticks 100, a lava bucket 20000 (leaving the bucket), a coal block 16000.
- **Crafting:** the grid trims to its filled cells; shaped recipes match as written or mirrored; shapeless ones pair ingredients off with the items (bipartite matching). The result slot gives its whole stack or nothing. Two damaged tools of one kind repair to the sum of their durability plus 5% of the maximum.
- **Sleeping:**
  - at night (sky darkening ≥ 4) or in a thunderstorm (M11); no monsters within 8 blocks horizontally or 5 vertically (M12);
  - the player lies at the head `(x + 0.5, y + 0.6875, z + 0.5)`; after 100 ticks asleep the time skips to the next multiple of 24000;
  - first person looks along the bed from the pillow; third person draws the model laid along the bed, `T(−facing·1.52)·Ry(south 90, west 0, north 270, east 180)·Rz(90)·Ry(270)`.
- **Signs** (SignRenderer): `T(0.5, 0.5, 0.5)·Ry(−rotation)` (wall signs then `T(0, −0.3125, −0.4375)`); hanging signs `T(0.5, 0.9375, 0.5)·Ry(−rotation)·T(0, −0.3125, 0)`. The back side turns 180°. Text: `T(0, 0.3333, 0.0467)·S(1/96, −1/96, 1/96)` (hanging: `T(0, −0.32, 0.073)` and 0.9/64), lines 10 (9) apart from −20 (−18), centered by `−width/2`, at most 90 (60) pixels. Plain text is the dye color × 0.4 in the block's light; glowing text is full-bright, outlined in that dark color (cream F0EBCC for black) within 16 blocks. The editor (SignEditScreen) draws the board 96×48 at `(w/2 − 48, 66)` and text at y 90, scale 0.9765628 (hanging: a 16-px picture at 4.5× and text at y 125); the cursor blinks every 6 ticks.

## Mobs (`entity/mob`, `entity/ai`, `entity/path`, `world/level/NaturalSpawner`)
| Mob | HP | Speed | Size (eyes) | Goals (priority) | Notes |
|---|---|---|---|---|---|
| pig | 10 | 0.25 | 0.9×0.9 (0.765) | float 0, panic 1.25 1, breed 3, tempt 1.2 4, follow parent 1.1 5, stroll 6, look 7, look around 8 | carrots, potatoes, beetroots (and carrots on sticks tempt) |
| cow | 10 | 0.2 | 0.9×1.4 (1.3) | float, panic 2.0, breed, tempt 1.25, follow 1.25, stroll, look, look around | wheat; milked with a bucket |
| sheep | 8 | 0.23 | 0.9×1.3 (1.235) | as the cow (panic 1.25, tempt and follow 1.1), eat grass 5 | wheat; 1–3 wool when sheared |
| chicken | 4 | 0.25 | 0.4×0.7 (0.644) | as the cow (panic 1.4, tempt 1.0, follow 1.1) | seeds; egg every 6000–12000 ticks; falls ×0.6; water costs nothing |
| zombie | 20 | 0.23 | 0.6×1.95 (1.74) | attack 2, stroll 7, look 8, look around 8; targets: hurt by 1, player 2 | 3 damage, armor 2, follow range 35, burns, babies ×1.5 speed |
| skeleton | 20 | 0.25 | 0.6×1.99 (1.74) | restrict sun 2, flee sun 3, bow or melee 4, stroll 5, look 6; targets as the zombie | bow every 40 ticks (Hard 20) after a 20-tick draw |
| creeper | 20 | 0.25 | 0.6×1.7 | float 1, swell 2, melee 4, stroll 0.8 5, look 6; targets: player 1, hurt by 2 | fuse 30, power 3 (charged 6) |
| spider | 16 | 0.3 | 1.4×0.9 (0.65) | float 1, leap 0.4 3, melee 4, stroll 0.8 5, look 6; targets: hurt by 1, player in the dark 2 | climbs walls, ignores cobwebs, gives up in light (1/100) |

- **Tick** (Mob.tick): despawn check, previous values, fluids and rain, the shared living environment (`LivingEntity::environmentTick`: invulnerability, landing, air, fire, lava, blocks, void), death (loot at once, vanish after 20 ticks), then aiStep: tiny speeds zeroed, the AI (sensing, the target and goal selectors in full every other tick, `(tickCount + id) % 2`, else only the goals that tick every tick; navigation; move, look and jump controls), jumping (`max(0.42·jumpFactor, vy)`, 10-tick delay; +0.04 in fluids), input ×0.98, travel, shoving, the mob's own step (burning, growing, eggs), the body rotation and the walk animation.
- **Travel** (LivingEntity.travel): on land `speed·0.216/f³` with input `(xxa, 0, zza)` where zza = speed (mobs move at speed² × modifier), friction `f·0.91`, gravity 0.08 with drag 0.98; ladders cap at 0.15; in water 0.02 and drag 0.8 (sinking `g/16`); in lava 0.02 and drag 0.5; out of a fluid against a wall a 0.3 hop.
- **Controls:** MoveControl turns the body up to 90° a tick toward the wanted point and jumps when it is higher than a step or blocked by a non-door, non-fence block; STRAFE moves sideways unless that leaves walkable ground. LookControl turns the head (10°/40° a tick by default) for 2 ticks after each request, else back toward the body; walking keeps it within 75° of the body. BodyRotationControl: moving, the body faces the yaw; standing, it follows a head turned over 15°, and after 10 ticks eases toward it over 10 more.
- **Goals:** GoalSelector stops goals that can't continue, starts those whose flags (move, look, jump, target) are free or held by an interruptible goal of a worse priority (lower number wins), then ticks the running ones; goal delays halve (`reducedTickDelay`) since they tick every other tick. Panic runs to a random spot within 5 (4 up or down) for 40 ticks after a panic-causing hit (attacks, fire, lava, cactus, lightning, explosions), or to water within 5 when burning. Stroll: 1 in 60 checks (unless idle 100+ ticks), the best of 10 random spots within 10 (7 up or down) by the walk target value (animals: grass 10, else brightness − 0.5; monsters: 0.5 − brightness), on land only (`landRandomPos`) 99.9% of the time. Melee: re-paths every 4–11 ticks (more when far), attacks every 20 ticks within reach (the box grown by 0.828 sideways). Targeting: within the follow range (×0.8 sneaking, ×0.5 wearing their own head), in sight (a collider ray from eye to eye, at most 128), 1 in 5 checks; forgotten out of range or after 3 (hurt by: 15) seconds unseen.
- **Path finding** (`entity/path`, WalkNodeEvaluator and PathFinder): A* over block nodes; the heuristic is 1.5 × the distance to the target, costs the step length plus the node's malus; at most `followRange·16` nodes and `followRange` blocks walked. A node's type is the worst over the mob's box (`⌊w+1⌋` wide, `⌊h+1⌋` high): open air over a floor is WALKABLE, unless next to fire, lava, cactus or water (DANGER/WATER_BORDER); fences, walls and closed gates are FENCE (−1), closed wooden doors −1 (no door opening), leaves −1, water 8, fire danger 8, damage-fire 16. Neighbors: the four sides (up a step to 1 block, or 1.125 with the jump check, down to the fall limit: 3, or more with a target and spare health), then diagonals past two accepted sides no higher than the node. Ground navigation moves the target down to the ground (or up out of a block), steers to each node's center (advancing within `max(w/2, 0.75 − w/2)` or when the next node lies the other way), gives up when it moved less than `speed²·25` blocks in 100 ticks or took 3× longer than expected for a node; skeletons by day cut paths at the open sky; spiders without a path head straight for the target.
- **Spawning** (`NaturalSpawner`): each tick (animals every 400) every ticking chunk within 128 blocks of the player (a spectator stops it) tries one spot: random x, z, and y from the bottom to one above the surface, not inside a full opaque block; three packs, each up to `⌈4r⌉` (then the type's group size) steps of `rand(6) − rand(6)`; the spot must be over 24 blocks from the player and the world spawn, its floor sturdy and not bright (emission < 14, not glass, leaves, ice, barriers, bedrock, magma), the spot and the one above free of full collision, fluids, rails, redstone and harm, the type's rule (animals: grass below and raw brightness > 8; monsters: not Peaceful, sky light ≤ rand(32), block light 0, darkness ≤ rand(8)), the mob's box clear, its walk target value ≥ 0, no liquid. Caps: `cap·spawnable chunks/289` counted over non-persistent mobs. New chunks: while `rand < 0.1` (0.07 in snowy plains and ice spikes) a pack of the biome's animals at the surface, retrying 4 times each.
- **Despawning** (checkDespawn): beyond 128 blocks at once; beyond 32 after 600 idle ticks, 1 in 800 per tick; idle time resets within 32 blocks; monsters gain 2 idle ticks a tick in the light (brightness > 0.5); animals never despawn.
- **Breeding:** food sets 600 ticks of love (only at age 0); two in love within 8 blocks walk together and after 60 ticks within 3 blocks make a baby (age −24000), the parents rest 6000 ticks; 1–7 experience. Feeding a baby adds `⌊(−age/20)·0.1⌋` seconds. Babies follow the nearest adult over 3 blocks away (within 8, 4 up or down) until within 3 or beyond 16.
- **Undead in daylight:** each tick when it is bright outside (darkening < 4), brightness `f > 0.5`, `30·rand < (f − 0.4)·2`, dry and under the open sky: a helmet wears by `rand(2)`, else 8 seconds of fire.
- **Skeleton bow** (performRangedAttack): an arrow from the eyes − 0.1 toward the target's lower third, aimed `0.2 × horizontal distance` higher, speed 1.6, spread `14 − 4·difficulty`, damage `2·power + 0.11·difficulty ± 0.574`.
- **Arrows** (AbstractArrow): drag 0.99 (water 0.6, before the move), gravity 0.05; the first block (collider shapes) or entity (boxes grown by 0.3, entering only, not the shooter until clear of it) on the segment; damage `⌈|v|·base⌉`, critical `+rand(d/2 + 2)`; in a block they back off 0.05, shake 7 ticks and stay a minute; a creative player bounces them off (×−0.1).
- **Player bow:** power `p = (f² + 2f)/3`, f the draw in seconds (at most 1); below 0.1 nothing; speed `3p` along the look plus the player's motion, spread 1, critical at `p = 1`; the FOV shrinks by `0.15·min(f, 1)²`.
- **Creeper:** the swell grows by 1 a tick while its target is within 3 blocks (ignited: always), shrinks once it is beyond 7 or unseen; at 30 it explodes. A fall adds `1.5 × distance` (up to 25). Drawn scaled `(1 + g⁴·0.4)·w` across and `(1 + g⁴·0.1)/w` up, `w = 1 + sin(100s)·s·0.01`, flashing white `0.75·⌊clamp(s, 0.5, 1)·15⌋/15` when `⌊10s⌋` is odd.
- **Rendering:** vanilla's LivingEntityRenderer transform `Ry(180 − body)·Rz(death flip)·S(−1, −1, 1)·T(0, −1.501, 0)`; the death flip is `min(√((t−1)/20·1.6), 1)` × 90° (spiders 180°). Baby models (BabyModelTransform): head parts moved by (0, y, z) (scaled by 0.75 when the head shrinks, humanoids), the rest scaled ½ about a pivot moved down 24: pig (4, 4), cow (8, 6), sheep (8, 4), chicken (5, 2), zombie (16, 0, head ×0.75). Shadows: pig, cow, sheep 0.7, chicken 0.3, humanoids 0.5, spider 0.8 (babies halved). Wool tint: white E6E6E6, other dyes ¾ of their texture color. Armor: the outer layer grown 1.0 (legs 0.9) for helmet, chestplate and boots, the inner 0.5 (legs 0.4) for leggings, posed like the body.

## Simulation (`world/sim`)
- **Updates:** `setBlockAndUpdate` = set, the new block's `onPlace`, `neighborChanged` for the six neighbors (W, E, D, U, N, S), then shape updates spreading out (budget 512). The level tick runs time, weather, scheduled block ticks then fluid ticks (65,536 each), chunk ticks (post-processing, lightning, precipitation, random ticks), entities, block entities.
- **Random ticks:** per section, `randomTickSpeed` (3) positions from vanilla's LCG `r = r·3 + 1013904223`, using `r >> 2` for (x, z, y) = bits (0-3, 8-11, 16-19).
- **Light checks:** "raw brightness" = max(sky − darken, block). Crops, stems, berries and bamboo use darken 0 (they grow at night under the sky); saplings and grass spreading use the current sky darkening.
- **Crops:** need raw light ≥ 9 at the crop. Chance per random tick `1/(⌊25/f⌋ + 1)` with `f = 1 + Σ` over the 3×3 farmland below: 1 dry or 3 wet, a quarter for the eight around; f halves when the same crop stands on both axes, or diagonally. Beetroot and torchflower crops skip a third of their ticks. Bone meal adds `2..5` (beetroot `/3`, torchflower 1).
- **Stems:** the crop rule; at age 7 a fruit on a random side (N, E, S, W) over farmland or dirt, and the stem attaches to it.
- **Saplings:** raw light (with darkening) ≥ 9 above and 1/7: stage 0 → 1, then the tree. Bone meal: 45%. Trees come from vanilla's `TreeGrower` table (oak: 10% fancy; spruce: 2×2 mega spruce or 50% mega pine; jungle 2×2; dark oak 2×2 only; flower-side bee variants), placed through the same features as world generation (`LiveRegion`). The sapling makes way, and returns if the tree doesn't fit.
- **Sugar cane, cactus:** grow while the block above is air and the plant is under 3 tall; age 15 → a new block (sugar cane) or, for cactus, a 10% (25% at 3 tall) flower at age 8.
- **Kelp, cave vines:** 14% (10%) per random tick while age < 25, into a water source (air); the old head becomes body. Cave vines bear berries 11% of the time.
- **Bamboo:** 1/3 per tick with air and light ≥ 9 above, up to 16 tall; leaves move up (small, then large with the one below small and the next none); `stage` becomes 1 past 11 tall (25%) or at 16.
- **Vines:** 1/4 per tick, a random direction: spread sideways (around corners), up when supported, or hang down a random subset of their sides; at most 4 vines in the 9×3×9 around.
- **Mushrooms:** 1/25 per tick, at most 4 more within 9×3×9, a 5-step random walk to a dark (raw light < 13) or mycelium spot.
- **Grass, mycelium:** die (to dirt) when the block above blocks light; else, with light ≥ 9 above, 4 tries at `(±1, −3..+1, ±1)` turn dirt that light reaches (and no water above) into grass.
- **Leaves:** a neighbor change schedules a check a tick later; distance = min over neighbors (logs 0, leaves their distance, else 7) + 1. Non-persistent leaves at 7 decay by random tick, dropping their loot.
- **Farmland:** wet (7) with water within (±4, 0..1, ±4) or rain above; otherwise moisture drops by 1 per random tick, then dirt unless a crop keeps it. A fall onto it tramples it with chance `fall − 0.5`.
- **Ice, snow:** ice melts at block light > 10, snow layers at > 11. Precipitation: per chunk tick, `randomTickSpeed` tries at 1/48; still water at the edge of open water freezes where cold enough with block light < 10, and rain in cold air lays snow up to `snowAccumulationHeight` layers. Cauldrons catch rain 1/20, snow 1/10.
- **Copper:** 1/17.6 per random tick (0.05688889): stays if any copper within Manhattan 4 is less weathered; else ages with chance `((older+1)/(older+same+1))²`, ×0.75 when unaffected.
- **Sponge:** breadth first from the sponge to depth 6, at most 65 cells (with itself): water sources and flowing water go, waterlogged blocks drain, kelp and seagrass drop; becomes wet if anything went.
- **Falling blocks:** `onPlace` and shape changes schedule a check 2 ticks on; the block falls when the one below is air, fire, fluid or replaceable. The entity (0.98 cube) falls with gravity 0.04 and drag 0.98, and on landing places itself where the block is replaceable, the one below isn't free and it can stand; else it drops its item (`doEntityDrops`). Concrete powder sets in or beside water. Anvils deal `min(⌊(⌈fall⌉−1)·2⌋, 40)` and chip with chance `0.05 + 0.05·(⌈fall⌉−1)`.
- **Fire:** ticks every 30-39; ages `+rand(3)/2`. Rain on it (or beside) puts it out with chance `0.2 + age·0.03`. Without fuel it dies on a bare floor past age 3. Neighbors burn by `rand(300 or 250 below/above) < burnOdds`; a burnt block becomes fire with `rand(age+10) < 5`, else air, and TNT lights. Spread to air in `(±1, −1..4, ±1)` with odds `(ignite + 40 + difficulty·7)/(age + 30)` against `rand(100 + 100·(dy−1 above 1))`.
- **Weather:** clear time counts down first; otherwise thunder toggles after 3,600-15,600 ticks (on) or 12,000-180,000 (off), rain after 12,000-24,000 (on) or 12,000-180,000 (off). Levels move 0.01 a tick; raining above 0.2, thundering above 0.9. Sky darkening `(1 − c·(1 − 5r/16)·(1 − 5t/16))·11`.
- **Lightning:** 1 in 100,000 chunk ticks while thundering, at the surface of a random column (or the player in the open within 3 blocks), only where it rains. Life 2, then 1-3 more strikes after up to 10 ticks each; fires on Normal and Hard (the spot plus 4 around); 5 damage and burning within 3 (9 up). The bolt: 8 segments of 16 blocks wandering ±5 (forks ±15), 4 nested layers, color (0.45, 0.45, 0.5, 0.3) added.

## Weather rendering, particles and fog (`render/WeatherRenderer`, `entity/Particles`, `entity/DisplayTicks`)
- **Rain and snow:** every column within 10 blocks with precipitation gets a camera-facing quad from `max(camY−10, top)` to `max(camY+10, top)`; v = y/4 + offset. Rain scrolls by `−(t + j)/32·(3 + rand)`, snow drifts with gaussian offsets. Alpha `lerp(min(d²/100, 1), 1 (rain) or 0.8 (snow), 0.5)·rain`. Snow's light is `(l·3 + 15)/4`.
- **Rain splashes:** `100·rain²` tries per tick within ±10; rain particles on tops, smoke on lava, magma and lit campfires.
- **Display ticks:** 667 random blocks within 16 and 667 within 32 per tick run their `animateTick`.
- **Sky flash:** a bolt sets 2 ticks; the sky mixes toward (0.8, 0.8, 1) by `min(t, 1)·0.45` and the lightmap's sky factor is 1.
- **Fog in fluids:** water: start −8, end `96·max(0.25, vision)` (×0.85 in swamps), color the biome's water fog brightened by vision; vision ramps up over 600 ticks under water (`0.6·min(t/100, 1) + 0.4·clamp((t−100)/500)`). Lava: 0.25 to 1 (spectators −8 to half the render distance), color (0.6, 0.1, 0); no sky.

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

## UI (`ui/`, `render/ItemRenderer`, `game/Menu`)
- **GUI scale:** the largest s (up to the option's maximum) with `w ≥ 320s` and `h ≥ 240s`. That gives 4 at 1920×1080 and 3 at 1280×720.
- **Fonts** (`resources/FontSet`):
  - `font/default.json` lists the providers in priority order: space (`" "` advances 4), then `include/default` (`nonlatin_european`, `accented`, `ascii`), then unifont (not in the pack). The first provider with a glyph wins.
  - Bitmap glyphs:
    - cell size is the image size / the `chars` grid; scale = `height` / cell height;
    - `advance = (int)(0.5 + actualWidth·scale) + 1`, where actualWidth is the rightmost non-transparent column + 1;
    - the glyph's top is at `y + 7 − ascent`, so ASCII sits on the line and `accented` reaches 3 pixels above it.
  - Characters with no glyph draw the missing box: a 5×8 outline that advances 6.
  - § codes:
    - colors `0-9 a-f` reset the other formats;
    - `k` obfuscated (a random glyph of the same width, the same one in the shadow), `l` bold (drawn twice 1 pixel apart, advance +1), `m` strikethrough (y+3.5..4.5), `n` underline (y+8..9), `o` italic (top shifted `1 − 0.25·y`), `r` reset;
    - unknown codes are dropped.
  - The shadow is drawn first at (1, 1), with RGB/4. A color whose alpha bits are all 0 counts as opaque.
- **Sprites** (`textures/gui/sprites`, `atlases/gui.json`), scaled by their `gui.scaling`:
  - `stretch`;
  - `tile` (repeated at the nominal size);
  - `nine_slice`: corners keep their size; edges and center tile, or stretch with `stretch_inner`. Borders shrink to half the target when it is smaller, and a target exactly the nominal width or height collapses to three pieces.
- **HUD** (vanilla Gui):
  - hotbar at `(w/2 − 91, h − 22)` and the selection at `(w/2 − 92 + 20·slot, h − 23)`; items at `(w/2 − 88 + 20·i, h − 19)`;
  - hearts from `(w/2 − 91, h − 39)` with an 8-pixel step, shaking at 4 health or less; food from the right edge `w/2 + 91 − 8i − 9`; armor 10 above the hearts; air above the food;
  - the XP bar is `(w/2 − 91, h − 29)` 182×5, with `filled = (int)(progress·183)`;
  - the selected item's name shows for 40 ticks (alpha `timer·256/10`, capped) at `h − 59` (`h − 45` in creative);
  - the crosshair blends `ONE_MINUS_DST_COLOR, ONE_MINUS_SRC_COLOR`; under it, while the attack strength recovers, a 16×4 bar at `(w/2 − 8, h/2 + 9)` filled `(int)(s·17)`; or (option Hotbar) an 18×18 sword at `(w/2 + 97, h − 20)` filled from the bottom `(int)(s·19)`;
  - hearts blink (`(blinkTime − ticks)/3` odd) for 20 ticks after health drops while invulnerable, 10 after it rises; the containers turn white and the health just lost shows as `*_blinking` hearts under the rest.
- **Item models** (`resources/ItemDefinitions`, `ItemModelGenerator`, `entity/model/ItemGeometry`):
  - Definitions are trees of `model` (with tints), `condition`, `select`, `range_dispatch` (the last threshold ≤ value·scale), `special`, `composite`, `empty` and `bundle/selected_item`.
  - A select or range with no match falls back to its fallback, else the missing model.
  - `item/generated` layers become the full layer (south uv 0..16, north mirrored) between z 7.5 and 8.5, plus one side element per row or column of edge texels. These are vanilla's spans: each covers min..max across gaps.
  - Display transform: `T(translation/16)·Rx·Ry·Rz·S(scale)·T(−0.5)`, with translation clamped to ±80 px and scale to ±4. Left hands negate translation x and rotation y and z; a missing left transform copies the right one.
  - Specials draw the block entity's model as if it stood facing south:
    - the bed head plus its foot at z −1;
    - a head turned 180°;
    - a shulker box opening upward;
    - the shield and trident scaled (1, −1, −1).
- **GUI item lighting:**
  - The level lights `(0.2, 1, −0.7)` and `(−0.2, 1, 0.7)` are turned for icons. For blocks (`gui_light: side`) the matrix is `S(1,−1,1)·RotYXZ(1.0821041, 3.2375858, 0)·RotYXZ(−π/8, 3π/4, 0)`; for flat items (`front`) it is `RotY(−π/8)·RotX(3π/4)`.
  - Normals are taken after the GUI's `S(16, −16, 16)` (y negated). `shade = min(1, 0.4 + 0.6·Σ max(0, n·l))`.
  - Each icon gets its own slice of the depth range, so later icons always draw in front.
- **Items in hand:**
  - First person (`render/FirstPersonRenderer`, vanilla ItemInHandRenderer): depth is cleared and drawn with a 70° projection. The view-space pose is `Rx((pitch − xBob)·0.1)·Ry((yaw − yBob)·0.1)`, where `xBob += (pitch − xBob)·0.5` per tick.
  - Items (s = swing):
    ```
    T(±(−0.4·sin(√s·π)), 0.2·sin(√s·2π), −0.2·sin(s·π))
    T(±0.56, −0.52 − 0.6·equip, −0.72)
    Ry(±(45 − 20·sin(s²π)))·Rz(∓20·sin(√s·π))·Rx(−80·sin(√s·π))·Ry(∓45)
    ```
  - The bare arm uses vanilla renderPlayerArm's constants (`0.64, −0.6, −0.72`, then `T(∓1, 3.6, 3.5)·Rz(±120)·Rx(200)·Ry(∓135)·T(±5.6, 0, 0)`), with the arm at zRot 0.1.
  - The equip height eases toward 1 (the main hand: the attack strength cubed; 0 while a different item is shown) by at most 0.4 per tick. The shown item swaps once the height is below 0.1.
  - Third person (ItemInHandLayer): `arm pose·Rx(−90)·Ry(180)·T(±1/16, 0.125, −0.625)`, then the third-person display transform.
- **Menus** (`game/Menu`, vanilla AbstractContainerMenu):
  - pickup and split clicks: right click takes half rounded up, or places one;
  - shift-click quick moves through each menu's `quickMoveStack` rules;
  - drags (QUICK_CRAFT start, slots, end; types split evenly, one each, or full stacks in creative);
  - double click collects partial stacks first, then full ones;
  - number keys and F swap with the hotbar and the off hand;
  - Q throws (Ctrl+Q the stack); clicking outside the window drops the cursor stack;
  - middle click clones in creative.
  - Containers hold up to 99 per slot and armor slots 1.
  - The creative screen implements ItemPickerMenu clicks: copies of tab items, shift for a full stack, the same item adds one, and shift-clicking the hotbar deletes. The survival-inventory tab has a destroy slot.
- **Containers:** chest screens draw `generic_54.png`: the top `rows·18+17` px, then the bottom 96 px from v=126.

## Menus and the client (`app/Game`, `app/WorldSession`, `ui/`)
- **Structure:** `Client` holds what outlives a world (window, jobs, assets, registries, GUI, chat, options); `WorldSession` is one open world; `Game` runs the loop and a screen stack (set, push, pop) that changes only between callbacks. Screens reach the game through `ScreenHost`.
- **Frame limit** (vanilla getFramerateLimit): the Max Framerate option in a world (260 = unlimited), 60 in menus; vsync paces itself.
- **Mouse look:** degrees per count `(0.6·s + 0.2)³·8·0.15` for sensitivity s.
- **Menu background** (Screen.renderBackground): without a world, the panorama; then the blur; then `menu_background` (or `inworld_menu_background`) tiled at 32 px. Lists draw `menu_list_background` with `u = right, v = bottom + scroll`. Container screens and the chat line don't blur.
- **Blur** (`post_effect/blur.json`, `post/box_blur.fsh`): three across-then-down passes of a box blur of radius r (the Menu Background Blur option, default 5) in framebuffer pixels. Bilinear sampling reads two texels per sample at offsets `−r+0.5, −r+2.5, … ≤ r`, plus texel r at half weight, divided by `r + 0.5`. It blurs everything drawn so far, the HUD included.
- **Panorama** (PanoramaRenderer, CubeMap): an 85° view looking 10° down, turned right by `spin`, which grows 0.1° per tick of real time. Faces k = 0..3 are yaw 0, +90, 180, −90 and 4/5 up/down (vanilla grabPanoramixScreenshot). The pack's faces are 1×1, so the game renders its own six 90° views (1024²) of a fixed world: seed 42, eye (−60, 79, −250), time 2500, render distance 10.
- **Title screen:** the logo's top 44 rows of 256×64 at `(w/2 − 128, 30)`, the edition `(w/2 − 64, 67)` 128×14 of 128×16. Buttons from `l = h/4 + 48`: 200-wide rows at l, l+24, l+48; Options and Quit (98 wide) and the 20×20 icon buttons at l+84. The splash: `T(w/2 + 123, 69)·R(−20°)·S(f)` with `f = (1.8 − |sin(ms%1000/1000·2π)|·0.1)·100/(width + 32)`, centered at (0, −8), yellow; texts/splashes.txt minus one line, with fixed lines on 24 Dec, 1 Jan and 31 Oct.
- **World list** (SelectWorldScreen, WorldSelectionList): search box `(w/2 − 100, 22)` 200×20; rows 36 apart (32 drawn), 270 wide, from y 48 to `h − 64`, row tops `48 + 4 − scroll + 36i`, content at `w/2 − 133`. Entry: the 32×32 icon (or `misc/unknown_server`), name at +35/+1, `folder (M/d/yy, h:mm a)` at +12 and the mode line at +21 in 0xFF808080 (Hardcore Mode in 0xFFFF0000). The selection box is white while the list has focus, else 0xFF808080. A click on the icon, or two clicks within 250 ms, plays. Wheel: 18 px per notch. Buttons: Play/Create 150 wide at `h − 52`; Edit, Delete, Re-Create, Back 72 wide at `h − 28`.
- **Create World** (CreateWorldScreen, TabNavigationBar, GridLayoutTab):
  - tabs 24 high, `width = roundUp2((min(400, w) − 28)/3)` from `x = roundUp2((w − (min(400, w) − 28))/2)`; a selected tab shows the menu background inside it and a 1-px underline;
  - each tab's grid is centered across and placed `(int)((area − height)/6)` below the tab bar, in the area between the bar and the 33-row footer;
  - Game: a 210 column, rows 8 apart, labeled name box (label, 4, 208×20), Game Mode, Difficulty, Allow Commands. World: Type and Customize (150 each, 10 apart), the labeled 308-wide seed box, and the switch grid (a label, and a 44-wide ON/OFF button at the right; rows 4 apart after an empty one). More: three 210-wide buttons;
  - the footer's two 150-wide buttons are 8 apart, centered in its 33 rows;
  - seeds: a number as is, other text by Java `String.hashCode`, empty for random. Folders: illegal characters become `_`, then ` (1)`, ` (2)`… (FileUtil.findAvailableName).
- **Confirm** (ConfirmScreen): the message wrapped at `w − 50`; `titleTop = clamp((h − lines·9)/2 − 29, 10, 80)`, the message 20 below it, the buttons `clamp(messageTop + lines·9 + 20, h/6 + 96, h − 24)`.
- **Loading** (LevelLoadingScreen): `N%` at `h/2 − 34` over a map of the render-distance square, 2 px per chunk, in ChunkStatus colors (empty 545454, noise D1D1D1, features 21C600, initialize_light CCCCCC, light FFE0A0, full FFFFFF). The game is shown once the view around the player is meshed.
- **World icon** (tryTakeScreenshotIfNeeded): once a saved world's whole view is drawn and it has no `icon.png`, the frame's middle square (before the HUD) is box-filtered to 64×64.
- **Leaving a world:** "Saving world" shows for a frame, the world saves, and the title screen returns.
- **Commands:** vanilla permission levels: `/help`, `/list`, `/me` and `/seed` for anyone; the rest need "Allow Commands" (the harness's worlds always have it). Others get "Unknown or incomplete command".

## Polish (M13: `world/mesh/VisGraph`, `render/ChunkRenderer`, `ui/ScreenOverlays`, `game/KeyBindings`, `app/ActionScript`)
- **VisGraph** (cave culling): a section's cells that are opaque full cubes block sight. Fewer than 256 of them: every face sees every other; all 4096: none. Otherwise flood-fill from every open boundary cell; each region links all the faces it touches (36 bits, `a·6 + b`).
- **Occlusion search** (SectionOcclusionGraph full update): BFS from the camera's section (from outside the world's height: every column's top or bottom section in range, nearest first, entered from above or below and heading outward). A node keeps the directions it was entered from (`sources`) and every direction its path took (`dirs`). From a node it goes on in direction d when:
  - `opposite(d)` is not in `dirs` (no turning back along an axis);
  - some source `s` has `visible(opposite(s), d)` in the section's VisibilitySet (the camera's section sees everywhere);
  - the neighbor is within the render distance's cylinder (`(rd·16 + 16)` from chunk centers);
  - for nodes more than 60 blocks from the camera on any axis: from the neighbor's corner nearest the camera (offset along d's axis away from it), steps of 28 blocks toward the camera must only pass sections already reached until within 60.

  A reached neighbor gains d as another source. Columns without meshes are reached but not entered. The renderer then frustum-tests what was reached.
- **View bobbing** (bobView), after the roll, on the view and on the hands:
  ```
  walkDist += horizontalDistanceMoved·0.6;  bob += (min(0.1, horizontalSpeed if onGround else 0) - bob)·0.4
  g = -(walkDist + (walkDist - walkDistO)·pt),  h = lerp(pt, oBob, bob)
  translate(sin(gπ)·h/2, -|cos(gπ)·h|, 0) · rotZ(sin(gπ)·h·3°) · rotX(|cos(gπ - 0.2)·h|·5°)
  ```
- **Screen overlays** (ScreenEffectRenderer): quads at z = −0.5 spanning −1..1 in the hands' 70° projection, mapped to GUI pixels. In a block: its particle sprite, color 0.1, U mirrored. Under water: `misc/underwater` repeated 4×, offset `(−yaw/64, pitch/64)`, color (b, b, b, 0.1) with b the eyes' brightness. Burning: two `fire_1` quads translated `(∓0.24, −0.3)`, turned ±10° about y, alpha 0.9.
- **Vignette** (Gui.renderVignette, Fancy only): `vignetteBrightness += (clamp(1 − brightness(max(sky − skyDarken, block)), 0, 1) − vignetteBrightness)·0.01` per tick, at the eyes; drawn full screen with blending `(ZERO, ONE_MINUS_SRC_COLOR)` and color `(v, v, v)`. `brightness(l) = f/(4 − 3f)`, `f = l/15`.
- **Particles option** (addParticleInternal): nothing beyond 32 blocks of the camera; Decreased drops one in three (`random.nextInt(3) == 0`), Minimal everything; terrain debris and item pieces are exempt.
- **Key bindings:** GLFW codes, saved by vanilla's InputConstants names (`key.keyboard.left.shift`, `key.mouse.left|right|middle`, `key.mouse.N` for button N−1, `key.keyboard.unknown`). The Key Binds list sorts by category (movement, gameplay, inventory, creative, multiplayer, ui, misc), then by translated name; rows 20 apart in a 340-wide list, Reset (50) 10 left of the scrollbar and the key button (75) 5 left of that, 2 above the row. A key in use twice reads `§c[ §fKEY§c ]` with a 3-wide red bar 6 left of its button and the "also used for" tooltip; the key waiting for input reads `§e> §f§nKEY§r§e <`.
- **Action script** (`--script`): runs in the world's tick before the player acts, and only ever sets the player's look, holds controls, adds attack and use clicks, and clicks menu slots (`Menu::clicked`). Moving uses the mob pathfinder (a 0.6×1.8 mob; a path ending with the target within reach counts), else a staircase dug one step at a time: down (clear head, feet and below ahead), level, or up (clear above the head and two ahead, jump). Each step goes along the axis with the most left to go first. Steps never go into lava, next to a fluid when a block must be broken, into a cell already dug through, or off more than a 2-block drop. Tools: the inventory's best for the block (harvests it, then fastest; bare hands over a tool that gains nothing), swapped into the hotbar with a number-key click. Crafting places one ingredient per cell (pick the stack up, right-click a cell, put the rest back), then takes the result and puts it down where it stacks. Smelting fills the input and fuel slots, waits for the output, and shift-clicks it out.
