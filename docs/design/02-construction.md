# Player-Created Buildings

One of the largest and hardest systems in the game. Players may **not** place
unrestricted geometry at launch — that creates performance, moderation,
collision, and networking problems. We use a **modular construction system**.

## Building workflow

1. Player purchases or leases land.
2. Server checks zoning rules.
3. Player selects a building category.
4. Player chooses a footprint.
5. Player places structural modules.
6. Player adds floors, walls, doors, windows, stairs, and elevators.
7. Player selects utility systems.
8. Player furnishes the interior.
9. Building is submitted for automated validation.
10. Server checks collision, access, performance, and prohibited content.
11. Building becomes active.
12. Owner can rent or sell rooms and units.

## Building categories

Houses, apartment buildings, office buildings, stores, restaurants, clubs,
warehouses, hotels, garages, factories, studios, event venues, gang
headquarters, medical clinics, schools, government offices.

## Construction constraints (enforced server-side)

Every building has:

- Maximum polygon budget
- Maximum object count
- Height restriction
- Zoning classification
- Fire exits
- Valid entrance
- Navigation paths (navmesh must generate)
- Collision validation
- Server performance rating
- Interior occupancy limit

## Validation pipeline

Submission → automated checks (collision, access, perf budget, prohibited
content) → activation. Failures return actionable errors ("no reachable fire
exit on floor 3"), never silent rejections. All placements are logged for
rollback (see 09-economy-and-data.md).

## Rental system

Building owners create rental units with:

- Monthly or weekly rent, security deposit, lease length
- Access permissions, furniture included/excluded, shared room access
- Storage allowance, business-use permissions
- Automatic payment, eviction rules
- Tenant ratings, maintenance responsibilities

## MVP construction scope

Players can build exactly four types: **small house, store, office, gang
clubhouse** — using one buildable-lot flow end to end.
