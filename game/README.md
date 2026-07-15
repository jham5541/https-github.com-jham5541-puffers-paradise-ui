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
| `Core/` | Game mode, player state (server-authoritative wallet) |
| `Characters/` | Third-person GAS character, presets + selection subsystem |
| `Abilities/` | Gameplay Ability System: health/stamina/armor attributes |
| `Heat/` | The heat (law-enforcement response) component |
| `Vehicles/` | Chaos Vehicles base pawn with chase camera + driving input |
| `Weapons/` | Server-validated hitscan weapon component |
| `AI/` | Ambient NPC + AI controller with crime-witnessing perception |
| `Missions/` | Mission data assets + replicated per-player progress |
| `Interaction/` | Interactable interface (vehicles, doors, mission givers) |

Phase 1 scope lives here (see `docs/production/roadmap.md`): character
controller, camera, one vehicle, one weapon, one NPC, one mission, small test
map, basic multiplayer. Content (`Content/`) is tracked via LFS.
