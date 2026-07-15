# Multiplayer Architecture

A GTA-style game cannot safely trust the client.

## Server-validated actions

Movement, weapon fire, damage, purchases, inventory changes, property
ownership, vehicle ownership, construction placement, mission rewards,
organization permissions, rental access.

The client predicts; the server decides. Any client-reported value that
touches money, ownership, or damage is treated as a *request*, validated
against server state, and rejected with a correction if invalid.

## Player counts

| Stage | Players per server |
|---|---|
| Prototype | 32 |
| First commercial version | 64 |
| After optimization | 100–128 |

Do **not** promise thousands of players in one seamless city during early
development.

## Topology

- UE5 dedicated servers, one per world instance
- Replication Graph for relevancy at high player counts
- Instanced interiors run in the same server process where possible;
  heavy venues (nightclub events) can be split to sub-instances later
- Backend services (auth, persistence, economy) sit behind the game server;
  the game client never talks to the database directly

## Anti-cheat and integrity

- Server authority (above) is the primary defense
- Sanity bounds on movement/rate-of-fire server-side
- Transaction logs for economy rollback
- Platform anti-cheat (EAC or similar) evaluated at Phase 4
