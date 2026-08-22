# Puffers Paradise — Empire City (web build)

Browser open-world crime sandbox built on Three.js. Everything is procedural —
no external model, texture, audio, or font assets.

## Setup

```bash
cd web
npm run setup     # installs three + playwright-core, vendors three into vendor/
npm run serve     # http://localhost:8080/
```

`vendor/` is generated (git-ignored) — `npm run vendor` recreates it from
`node_modules/three`. The importmap in `index.html` resolves `three` and
`three/addons/` to that directory, so the game runs from any static server with
no bundler.

## Layout

| Path | Purpose |
|---|---|
| `GAME_SPEC.md` | The build contract: module APIs, shared `G` object, event names, art direction, and the full character sheet descriptions for all 14 playable characters |
| `src/core/` | Engine plumbing: event bus, RNG + spatial hash + texture helpers, input, audio, postfx |
| `src/world/` | City generation and atmosphere (sky, sun, weather, day/night) |
| `src/entities/` | Vehicles, pedestrians, and the playable characters (`characters/<id>.js` per character) |
| `src/game/` | Player controller, wanted/police system, missions |
| `src/ui/` | HUD and menu |
| `tools/` | Verification harnesses (see below) |

## Verification

```bash
node tools/shot.mjs        # 10 gameplay/menu frames -> tools/shots/, fails on console errors
node tools/charshot.mjs    # per-character studio renders -> tools/charshots/
node tools/playtest.mjs    # drives real input, asserts against window.__game
```

Debug URL params: `?autostart=<id>&tod=17.5&cam=street|high|orbit|vista&weather=rain&seed=42&unlock=all&nofx=1`

## Characters

Five starters (Purp, Smoke, Preme, Dime, Nug) plus nine unlockables (Baller,
Cream Cake, Scraps, Herb, Slick, Betty Baaad, Maestro, Velvet, Seven). Each is
built from the official character sheets — the authoritative descriptions live
in `GAME_SPEC.md` section 5 and must be matched exactly, at photoreal material
quality.
