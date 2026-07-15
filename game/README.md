# The Puffers Club — UE5 Project

## Requirements

- Unreal Engine **5.4+** (source build required later for dedicated servers;
  launcher build is fine for Phase 1 client/editor work)
- Visual Studio 2022 (Windows) or clang toolchain (Linux servers)
- Git LFS (`git lfs install`) — binary assets will not clone correctly without it

## Getting started

1. `git lfs install && git lfs pull`
2. Right-click `PuffersClub.uproject` → *Generate Visual Studio project files*
3. Build the `PuffersClubEditor` target, then open the `.uproject`

## Targets

| Target | Purpose |
|---|---|
| `PuffersClub` | Game client (Steam, later iOS) |
| `PuffersClubEditor` | Editor |
| `PuffersClubServer` | Dedicated server (Linux in production) |

## Source layout (`Source/PuffersClub/`)

| Area | Contents |
|---|---|
| `Core/` | Game mode, game state, player state |
| `Characters/` | Third-person character, movement, input |
| `Abilities/` | Gameplay Ability System: attributes, ability stubs |
| `Heat/` | The heat (law-enforcement response) subsystem |
| `Economy/` | Client-side wallet mirror; server-authoritative hooks |

Phase 1 scope lives here (see `docs/production/roadmap.md`): character
controller, camera, one vehicle, one weapon, one NPC, one mission, small test
map, basic multiplayer. Content (`Content/`) is tracked via LFS.
