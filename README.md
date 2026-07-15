# The Puffers Club

**Working title: Empire City Online**

A persistent 3D open-world crime, business, social, and property-building game.
The core differentiator: the city is partly **player-built and player-operated**,
not a fixed open world.

Target platforms: **Steam (PC)** first, **iOS** as a follow-on client (see
[docs/platforms/steam-and-ios.md](docs/platforms/steam-and-ios.md) for the honest
constraints there).

## What players can do

- Complete cinematic main missions, side missions, and dynamic world events
- Form teams, clubs, gangs, crews, companies, and political groups
- Buy, rent, design, construct, and manage buildings — and enter them
- Operate legal and illegal (fictionalized) businesses
- Host private or public servers with custom rules and economies
- Build neighborhoods and shape the city over time
- Trade assets, fight, race, socialize, work, invest, and govern

## Repository layout

| Path | Purpose |
|---|---|
| `docs/` | Game design, technical architecture, and production docs |
| `game/` | Unreal Engine 5 project (C++ source, config, content dirs) |
| `backend/` | Platform services: auth, persistence, economy, matchmaking (TypeScript) |
| `tools/` | Internal automation and asset pipeline scripts (Python) |

## Where to start

1. Read [docs/design/00-core-concept.md](docs/design/00-core-concept.md)
2. Read the MVP scope: [docs/production/mvp.md](docs/production/mvp.md)
3. Read the roadmap: [docs/production/roadmap.md](docs/production/roadmap.md)
4. Engine setup: [game/README.md](game/README.md)
5. Backend setup: [backend/README.md](backend/README.md)

## Guiding rules

- **Server-authoritative everything.** The client is never trusted with money,
  inventory, ownership, or damage.
- **One district before one city.** Ship a complete gameplay loop in a small,
  dense space before scaling out.
- **Original IP only.** Original name, city, story, factions, brands, vehicles,
  music, and UI. No GTA assets, names, or trade dress. See
  [docs/production/originality.md](docs/production/originality.md).
- **Fictionalized crime.** Illegal-business gameplay is abstract and
  game-mechanical, never instructional.
