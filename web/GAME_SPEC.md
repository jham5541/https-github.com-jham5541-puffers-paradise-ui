# PUFFERS PARADISE — "Empire City" Web Sandbox — BUILD SPEC (v1)

Open-world crime sandbox in Three.js. Target: the most visually striking,
polished, cohesive open-world sandbox ever shipped in a browser. Every module
is judged by a harsh visual critic against AAA reference footage. No
placeholder cubes, no flat lambert boxes, no default-blue sky. Everything is
procedural (zero external asset files) but must NOT look procedural.

## Hard rules for every agent

- ES modules only. Import three as `import * as THREE from 'three'` and addons
  as `three/addons/...` (importmap in index.html resolves them to `web/vendor/`).
- 1 unit = 1 meter. Y is up. Ground plane at y=0. City spans roughly
  x,z in [-600, 600].
- No network fetches, no external textures/models/fonts. Textures are generated
  with canvas 2D or DataTexture; geometry is built in code. Use
  `makeCanvasTexture` from `src/core/util.js`.
- Deterministic where possible: use `RNG` (mulberry32) from util.js with a seed.
- Performance budget: 60fps target on a desktop GPU. Instance repeated meshes
  (`THREE.InstancedMesh`), merge static geometry (BufferGeometryUtils
  `mergeGeometries`), cap draw calls. No per-frame allocations in update loops
  (reuse temp vectors).
- Shadows: one main shadow-casting directional light from atmosphere; only
  meaningful casters cast, everything receives.
- Own ONLY your assigned files. Never edit another module's file, core files,
  index.html, or main.js. Your module must import cleanly and self-contained.
- Every public function documented in this spec must exist with the EXACT
  signature. main.js is already written against these signatures.

## Shared context object `G`

main.js builds `G` and passes it to every factory:

```js
G = {
  scene, camera, renderer,            // three basics; camera = PerspectiveCamera(60)
  events,                             // tiny emitter: on(ev,fn), off, emit(ev,...args)
  input,                              // see core/input.js
  time: { dt, elapsed, timeOfDay },   // timeOfDay in [0,24) float, set by atmosphere
  world: { city, atmosphere },        // filled at boot
  systems: { vehicles, peds, police, missions, hud, audio, postfx },
  player: null,                       // set when game starts
  state: 'menu',                      // 'menu' | 'playing' | 'wasted' | 'busted'
  money: 500,
  quality: { pixelRatio, shadows: true, postfx: true },
  debug: { }                          // URL params parsed: autostart, char, tod, cam
}
```

Events used across modules (emit/subscribe via `G.events`):
- `'startGame' (characterId)` — menu -> main
- `'wanted' (level)` — police -> hud/audio
- `'moneyChanged' (amount)` — main/missions -> hud
- `'playerDamaged' (hp)` -> hud/audio
- `'playerDied' ()`, `'playerBusted' ()`
- `'missionUpdate' ({title, objective, blip:{x,z}|null})` — missions -> hud
- `'missionComplete' ({title, reward})`
- `'notification' (text)` — anything -> hud toast
- `'gunshot' ({x,z, byPlayer})` — player/police -> police(heat)/peds(flee)/audio
- `'explosion' ({x,y,z})`
- `'characterUnlocked' (id)` — unlock system -> menu

## Module contracts (one agent each)

### 1. `src/world/city.js` — THE CITY
```js
export function buildCity(G, seed) -> {
  group,            // THREE.Group added to scene by main
  colliders,        // SpatialHash (from util.js) of {x0,z0,x1,z1,h} static AABBs
  roadGraph,        // { nodes:[{x,z}], edges:[[i,j],...], laneW:3.2 } drivable centerlines
  sidewalks,        // array of {x,z} waypoint loops for peds
  spawns: { player:{x,z,heading}, vehicles:[...>=40], peds:[...>=80] },
  minimap,          // { canvas, worldToMap(x,z)->[u,v], size } top-down styled map
  update(dt),       // animated bits: traffic lights cycle, shop signs flicker
  lights,           // array of {x,y,z,color} lamp/neon positions for atmosphere night
}
```
Art direction: dense downtown core (30+ towers 40-140m, varied silhouettes:
setbacks, crowns, antennae, rooftop AC/water towers), midrise brick blocks,
low strip-mall/storefront edges, industrial docks south with cranes+containers,
a green park with paths/trees/pond, beach+ocean plane west with animated water
shader, hills backdrop far east. Roads: asphalt with center dashes, crosswalks,
worn tire tracks, manholes, curbs+sidewalk slabs, storm drains. Props:
streetlamps, traffic lights (working), fire hydrants, benches, dumpsters,
billboards (canvas ads for fictional brands: "Puffers Club", "Nug King",
"Green Diamond Dispensary", "Choom FM"), neon shop signs, palm + oak trees
(instanced, wind sway), parked-car spots. Window textures: emissive night
windows with varied warm/cool lit pattern per building; day reflective glass.
NO two adjacent buildings identical. Facades need real relief: inset windows,
cornice/ledge bands, protruding sills. Target <= ~400 draw calls.

### 2. `src/world/atmosphere.js` — SKY/LIGHT/WEATHER
```js
export function createAtmosphere(G) -> {
  update(dt),                  // advances timeOfDay (1 game hour ~ 60s), moves sun
  setTimeOfDay(h), setWeather('clear'|'rain'|'fog'),
  sun,                         // THREE.DirectionalLight (shadow-casting, 4k map)
  night01(),                   // 0=day 1=night helper for other modules
}
```
Full sky dome shader (Rayleigh-ish gradient, sun disc + glow, dawn/dusk oranges
and purples, night: stars + moon + city glow horizon). Fog color matched to sky.
Drifting clouds. Rain: instanced streaks + wet ground darkening + street-light
specular. Hemisphere + ambient tuned per time. At night, pooled PointLights
placed from `city.lights` nearest the camera. Daylight must produce crisp
contact shadows for every ped/vehicle/prop.

### 3. `src/entities/vehicles.js` — VEHICLES + TRAFFIC
```js
export function createVehicleSystem(G) -> {
  update(dt),
  spawnVehicle(type, x, z, heading) -> veh,   // 'sedan','sports','suv','taxi','police','van'
  vehicles,
  nearestEnterable(x, z, r) -> veh|null,
  // veh: { group, pos, heading, speed, driver:null|'player'|'ai'|'police',
  //        enter(who), exit(), applyDamage(n), health, dead,
  //        controls:{throttle,brake,steer,handbrake}, update(dt) }
}
```
Real automotive silhouettes from beveled side-profile shapes (hood-windshield-
roof-trunk), inset greenhouse with tinted glass, bumpers, grille, mirrors,
wheel arches; clearcoat paint (MeshPhysicalMaterial), chrome trim, detailed
wheels that spin and steer, emissive brake lights, headlight spots at night,
taxi sign, police lightbar with alternating red/blue. Arcade-weighty physics:
accel curves, speed-sensitive steering, handbrake drift, body roll/pitch,
collisions vs city colliders and other cars, damage -> smoke -> fire ->
`explosion`. Traffic AI: >=25 cars on roadGraph lanes, red-light stops,
obstacle braking, honks, panic swerve on `gunshot`.

### 4. `src/entities/peds.js` — PEDESTRIANS
```js
export function createPedSystem(G) -> {
  update(dt), peds,
  hitPed(ped, dmg, fromDir),
  nearestPeds(x,z,r) -> [ped...],
}
```
>=60 active peds streaming around the camera on sidewalks. Shaped human bodies
(torso with shoulders/hips, head with hair, jointed limbs with a real walk
cycle, opposing arm/leg swing, vertical bob), 20+ outfit palettes, varied
heights/builds/accessories (caps, bags, hoods). Behaviors: stroll, idle phone,
chat pairs, flee on `gunshot`/`explosion`, dodge cars, stumble from bumps,
death crumple that persists ~20s.

### 5. `src/entities/characters.js` — PLAYABLE CAST (registry)
```js
export const CHARACTERS = [ {id,name,tagline,desc,accent,locked?,unlock?}, ... ]
export function buildCharacter(id) -> { group, pose(state,t,speed), parts }
export function initUnlocks(G)
```
Per-character implementations live in `src/entities/characters/<id>.js`, each
exporting `buildModel() -> { group, pose, parts }`. Shared realism machinery in
`src/entities/characters/common.js`: `makeBudHead(opts)` (displaced core +
1500-3000 instanced calyx bracts + 150-400 curled pistils + trichome frost
points + sheen material), fabric texture factory (knit/denim/satin/fleece/
suiting/nylon puffer/plush fur with color+bump+roughness), skin material,
glossy cartoon-eye builder, teeth/lips, chain builder, joint/blunt/pipe with
ember + smoke. `pose(state,t,speed)` states: idle | walk | run | drive | punch |
aim | dead. Heads read ~1.35x oversized for the toy-figure proportions of the
sheets, but every material must be ULTRA-REALISTIC.

#### STARTER CAST (unlocked)
1. `purp` — **Purp** — cannabis-nug head (dense bumpy bud: base olive/tan
   #8a8a6a/#6b7250 with frosty white trichome speckle + orange pistil wisps
   #c77b3a), heavy-lidded bloodshot eyes w/ PURPLE lids (#6a3fa0), lit joint in
   mouth (white cone, glowing ember + smoke wisps), black brows. Shiny PURPLE
   puffer jacket (#9a6fc4, horizontal padded tubes, black center zipper, two
   grey leather chest pockets — one holds an orange RAW-style papers pack),
   silver cuban chain + Jesus-head pendant, light-wash baggy jeans, purple/white
   dunk-style sneakers. Hands in pockets idle. Vibe: relaxed, stoned.
2. `smoke` — **Smoke** — DARK charcoal bud head (near-black nugs #2e2e30 w/
   heavy orange pistils #d06a28 + frost), pointed flame-shaped top. GOLD square
   sunglasses (black lenses), huge glossy white ceramic cartoon grin (two rows
   of chunky teeth). Washed-black heavy hoodie (hood down) w/ small embroidered
   chest patch (green alien face in purple beanie), gold rope chain + gold
   Jesus pendant, black sweatpants, black/grey/white Jordan1-style sneakers.
3. `preme` — **Preme** — tall slim green bud head (olive/lime nugs, pink-red
   bloodshot cartoon eyes w/ bronze heavy lids, black brows), cartoon smirk w/
   white joint + glowing ember. GREEN bucket hat (#a8b96a) with black
   "SUPREME"-style logo + smoking-man icon. White boxy tee w/ single sky-blue
   chest stripe, gold chain + gold cross, olive-green sweatpants w/ logo patch,
   white/university-blue dunk lows. Vibe: laid-back hypebeast.
4. `dime` — **Dime** — female Puffers Club member: tan skin, hourglass figure,
   bud AFRO (purple/green frosty nugs) under glossy PINK vinyl beret, huge
   glossy cartoon eyes (purple iris, heavy pink lids, black lashes), big glossy
   pink lips. White satin bra top + white hip wrap, elbow-length white satin
   gloves, thigh-high glossy PINK patent platform stiletto boots (front zipper,
   gold heel), pearl necklace, small lavender purse w/ gold clasp.
5. `nug` — **Nug** — short & ROUND (1.5m, wide egg body): green/purple frosty
   bud head, gold aviator sunglasses w/ BLUE reflective lenses, big open
   laughing red-lipped mouth (teeth + tongue), tan cartoon human hands (one
   holds lit joint at side). Chartreuse/mustard knit button coat (brown
   buttons) over the round belly, green knit beanie w/ yellow band + yellow
   pom, purple-blue sweatpants, green/white classic sneakers.

#### UNLOCKABLE CAST — SET 1
Add with `locked:true` + `unlock:{type, hint}`. Menu shows locked entries as
dark silhouettes w/ padlock + hint; emit `'characterUnlocked' (id)`; persist to
localStorage `pp_unlocks`. URL param `unlock=all` unlocks everything (harness).

1. `baller` — **Baller** — unlock: win the "Smoke Show" street race. TALL
   (1.95m) athletic TAN-SKINNED human body, bold black tribal tattoo sleeve on
   right arm, gold wristwatch. Head: very tall tapering spire of dense
   purple/green bud (#5a4a62/#6b7250 + frost + orange pistils), two big
   cartoon eyeballs POPPING OUT on short stalks (bloodshot glossy white, brown
   iris, gold-ringed), huge wide smirk w/ big clean white teeth + tan cartoon
   lips. PURPLE basketball jersey ("THE PUFFERS CLUB" arc + big "24" in
   gold-outlined white, gold/purple trim), black basketball shorts w/
   purple/gold side stripe, white crew socks, purple/gold hi-top sneakers, gold
   rope chain w/ 3D nug-shaped pendant, colorful graphic-print backpack (green
   w/ pink splashes + orange jack-o'-lantern face patch).
2. `cream` — **Cream Cake** — unlock: complete "High Roller". EXTREMELY
   muscular (broad V-torso, thick arms flexing the suit) in a ROYAL BLUE slim
   suit (matte premium fabric, two buttons) over white dress shirt, red/white
   striped satin BOW TIE w/ "CREAM CAKE" lettering + glossy red knot, gold rope
   chain, white cartoon-gloves enamel lapel pin, red/white low sneakers. Head:
   dense dark purple/green bud w/ a few green leaves poking out, thick black
   comedy eyebrows, big glossy cartoon eyes (GREEN iris, pink bloodshot rims),
   shiny RED clown nose, exaggerated white cartoon FROWN mouth, joint at lip.
3. `scraps` — **Scraps** — unlock: collect 6 golden nugs. SMALL (1.3m) scruffy
   kid: shaggy droopy bud head hanging like a ragged beard (frosty purple/green
   scraggle, long stringy calyx strands), PURPLE knit beanie, oval wire
   sunglasses w/ YELLOW striped reflective lenses, red-lipped mouth w/ uneven
   YELLOW teeth (overbite). Dirty beige chore/lab coat (stains, purple round
   "T.P.C" chest patch), ripped faded skinny jeans (knee holes), destroyed
   grey/purple sneakers. Hands in pockets, hunched.
4. `herb` — **Herb** — unlock: complete all 6 story missions. SHORT ROUND
   ELDER (1.35m, gnome-like): light mossy-green bud head (fine dense moss
   texture + frost + orange pistils), LONG flowing WHITE beard to the belly +
   bushy white eyebrows, wooden tobacco pipe (bowl w/ glowing ember + wisps)
   held in one brown weathered hand. Rasta knit beanie (red/yellow/green
   bands), mustard-YELLOW satin mandarin-collar jacket w/ black toggle frog
   buttons + subtle sheen, olive-green loose pants, brown leather boots.
5. `slick` — **Slick** — unlock: escape a 3-star wanted level. Kid-height
   (1.45m) charmer: WIDE diamond/spade-shaped bud head (olive/rust nugs, heavy
   curly orange pistils), heavy-lidded pink stoned eyes, huge wide grin of
   YELLOW teeth w/ realistic tan lips. ORANGE prison jumpsuit (zip front, chest
   pocket, black stencil "420247" on chest, "PUFFERS CLUB" stencil across the
   back) over white tee, dirty white chunky sneakers, gold pinky ring, silver
   watch; one arm raised holding a lit blunt (brown wrap, ash tip, smoke).

#### UNLOCKABLE CAST — SET 2
1. `betty` — **Betty Baaad** — unlock: hold $10,000 cash. Plus-size sassy
   BROWN-SKINNED lady (1.6m, round curvy build): round purple/green frosty bud
   head, heavy-lidded sassy eyes (PINK iris, black lashes, pink lids), bold
   glossy RED cartoon lips, big GOLD hoop earrings, mustard-YELLOW knit beret.
   Purple sleeveless mini dress w/ printed white cartoon SHEEP + pink "I'M A
   BAAAAD B*TCH" text arc, bright GREEN satin bow at the collar, RAINBOW
   pleated tutu skirt at the hem, RAINBOW striped knee-high socks, PURPLE chunky
   platform mary-jane heels (ankle strap, gold buckle), purple wristbands. One
   hand on hip, other raised holding a lit blunt w/ smoke.
2. `maestro` — **Maestro** — unlock: complete "Dispensary Defense". Short round
   showman (1.4m): tall bud head under a BLACK felt TOP HAT w/ purple ribbon,
   gold-and-black DJ HEADPHONES around the head, big tan cartoon eyelids over
   pink-rimmed BLUE eyes, bright ORANGE glossy clown nose, huge grin w/ red
   cartoon lips + clean white teeth. PURPLE knee-length overcoat (open), DARK
   GREEN satin vest, white shirt, MAROON tie w/ yellow polka dots, purple
   PINSTRIPE baggy pants, shiny black leather shoes, WHITE cartoon gloves —
   arms-open welcoming pose.
3. `velvet` — **Velvet** — unlock: collect all 12 golden nugs. Tall smooth
   operator (1.8m): CONICAL camo-toned bud head (olive/tan/purple nugs, heavy
   rust pistils), sleepy asymmetric heavy-lidded eyes (one nearly shut), single
   square WHITE buck tooth, PINK felt round-brim hat w/ GOLD satin band.
   Light-GRAY PLUSH FUR suit — jacket and pants both fluffy (must read as real
   fur, not grey clay), white shirt, bright GREEN satin tie, gold cuban chain,
   hands ARE bud-textured (frosty nug fists), chunky WHITE sneakers.
4. `seven` — **Seven** — unlock: complete "Repo Bud". Baller #2 (1.7m): round
   mossy bud head, glossy ORANGE-RED eyes (amber iris, stoned), full plump
   PURPLE lips holding a lit joint sideways, BLACK snapback cap w/ purple "V7"
   logo. BLACK+PURPLE warm-up tracksuit: purple yoke/shoulders, purple "V" chest
   logo + number 7 on sleeve, "PUFFERS CLUB" arc printed on the back, purple
   side-stripe pants, thin gold chain, black/purple hi-top sneakers. Carries a
   PUFFERS CLUB orange basketball under one arm.

### 6. `src/game/player.js` — PLAYER CONTROLLER
```js
export function createPlayer(G, characterId) -> {
  update(dt), group, pos, heading, hp, armor, maxHp:100,
  inVehicle, weapon, damage(n, from), die(), respawn(x,z), camera:{ mode },
}
```
WASD camera-relative move, Shift run, Space jump, F enter/exit nearest vehicle
(<=4m), LMB punch/shoot (raycast vs peds/police/vehicles, muzzle flash light +
tracer + shell), RMB aim (over-shoulder zoom + reticle), R reload, Q cycle
weapon. Spring-arm third-person camera: 5.5m on foot / 7.5 driving / 2.3 aiming,
collision-shortened, damped, FOV kick with speed, idle handheld sway. When
`G.debug.cam` is set the harness owns the camera.

### 7. `src/game/wanted.js` — POLICE / WANTED
```js
export function createPoliceSystem(G) -> { update(dt), wanted, addHeat(n,x,z), cops }
```
Stars 1-5 per crime severity. Foot cops (navy uniform, cap, badge, pistols)
chase with LOS checks and shoot bursts; 3+ stars spawns police vehicles with
sirens; 4-5 adds heavier units. Busted when player is on foot, still, within 3m
of a cop for 1.5s. Evade timer decays stars. Emits `'wanted'` and `'siren'`.

### 8. `src/game/missions.js` — MISSIONS / PROGRESSION
```js
export function createMissionSystem(G) -> { update(dt), active, completed }
```
Glowing markers at giver spots (slim translucent beams, not blown-out slabs),
minimap blips via `'missionUpdate'`. Six story missions: "Special Delivery"
(timed drive), "Repo Bud" (steal a car, lose heat), "Smoke Show" (6-checkpoint
race vs 3 AI racers), "Dispensary Defense" (goon waves), "High Roller" (tail a
limo undetected, then intercept), "Cloud Nine" (shootout finale). Rewards $500
-> $5000, persisted to localStorage with completed ids. 12 golden nug
collectibles ($100 each, $2000 completion bonus), ATM robberies, payphone jobs.

### 9. `src/ui/hud.js` + `src/ui/menu.js` — UI
```js
export function createHUD(G) -> { update(dt), showReticle(b) }
export function createMenu(G, buildCharacter) -> { show(), hide() }   // emits 'startGame'
```
HUD: circular minimap (rotates with camera, player arrow, blips, view cone),
health/armor arcs on the rim, cash counter with roll animation, 5 wanted stars,
weapon+ammo chip, mission banner, toasts, aim reticle, damage vignette,
WASTED/BUSTED slams, controls hint. MENU: cinematic title over a live dusk city
pan, 14-card character carousel with locked-state silhouettes + unlock hints,
hero character on a lit rotating pedestal, name/tagline/desc + stat bars.

### 10. `src/core/audio.js` + `src/core/postfx.js`
```js
export function createAudio(G) -> { update(dt) }        // all-procedural WebAudio
export function createPostFX(G) -> { composer, update(dt), setQuality(q) }
```
Audio: city ambience bed, engine loop pitched by speed, footsteps, gunshots,
explosions, sirens, ped screams, mission fanfare, collect chime, and generative
"Choom FM" lo-fi radio in vehicles. PostFX: RenderPass, UnrealBloom (tight
threshold — no orb halos), vignette + subtle filmic grain + barely-perceptible
chromatic aberration, time-of-day color grade, FXAA, OutputPass.

## Boot order (implemented in main.js — DO NOT CHANGE)

city -> atmosphere -> postfx -> characters(menu) -> menu ... on 'startGame':
vehicles -> peds -> player -> police -> missions -> hud -> audio -> PLAYING.

## Debug URL params

`?autostart=purp&tod=17.5&cam=orbit|street|high|vista&nofx=1&seed=42&weather=rain&unlock=all`

## Verification harness

- `node tools/shot.mjs` — serves web/, drives Chromium, captures 10 gameplay
  and menu frames into `tools/shots/`. Fails on any console error.
- `node tools/charshot.mjs` — studio turntable renders of each character
  (`charview.html?id=<id>&view=full|head`) into `tools/charshots/`, for
  sheet-fidelity review.
- `node tools/playtest.mjs` — drives real input (move, drive, shoot) and
  asserts against `window.__game`.
