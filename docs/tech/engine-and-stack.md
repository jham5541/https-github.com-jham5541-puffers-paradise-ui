# Technology Stack

## Game engine: Unreal Engine 5

| System | Used for |
|---|---|
| World Partition | Streaming the open world |
| Nanite | Buildings and large environmental assets (PC) |
| Lumen | Lighting (PC; baked/simplified on iOS) |
| Chaos Vehicles | Driving |
| Gameplay Ability System (GAS) | Combat, skills, status effects, progression |
| Mass Entity | Crowds, pedestrians, traffic, ambient simulation |
| Common UI | Controller- and platform-friendly interfaces |
| MetaSounds | Dynamic audio |
| Dedicated server builds | Multiplayer |
| Replication Graph | Large player counts |

## Backend

- Unreal Engine dedicated servers
- Server orchestration: PlayFab, Hathora, Agones, or custom Kubernetes
- PostgreSQL — permanent game data
- Redis — active sessions, matchmaking, caching
- S3-compatible storage — user-generated content
- Node.js (TypeScript), Go, or C# backend services
- EOS or Steam for authentication and multiplayer services
- Vivox (or equivalent moderated provider) for voice chat
- Stripe for web purchases only, where platform rules permit
- Epic/Steam/console commerce systems for platform purchases

## Languages

- **C++** — core gameplay and networking
- **Blueprints** — rapid prototyping and mission scripting
- **TypeScript / Go / C#** — backend services (this repo starts with TypeScript)
- **Python** — internal asset tools and automation

## Repo conventions

- `game/` is the UE5 project. Binary assets (`.uasset`, `.umap`) go through
  **Git LFS** (`.gitattributes` is already configured).
- `backend/` services are containerized; `docker-compose.yml` brings up
  Postgres + Redis locally.
- CI must build: editor target, client target, and **dedicated server target**
  from day one.
