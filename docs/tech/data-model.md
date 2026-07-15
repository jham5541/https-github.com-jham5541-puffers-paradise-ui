# Data Model (initial sketch)

PostgreSQL is the source of truth. Redis holds sessions/matchmaking/cache.
S3-compatible storage holds user-generated content blobs (building layouts,
mission definitions).

## Core entities

```
accounts        — platform identity (EOS/Steam/Apple), standing, entitlements
characters      — per-account avatars: appearance, skills, reputation, record
inventories     — items with quantity, condition, storage location
vehicles        — ownership, registration, insurance, mods, storage
parcels         — land: district, zoning, owner/lessee
buildings       — parcel, category, validated layout ref (S3), perf rating
units           — rentable subdivisions of buildings
leases          — unit, tenant, rent, deposit, term, permissions, status
organizations   — type (team/club/gang/business), charter, treasury
org_roles       — custom roles with permission bitsets
org_members     — character ↔ org with role
businesses      — org-owned commercial state: licenses, inventory, pricing
missions        — static definitions + per-character progress
servers         — player-hosted server configs (template + overrides)
moderation      — per-server and platform-level actions, auditable
transactions    — append-only ledger of every money/ownership mutation
```

## Rules

- Money and ownership mutate **only** inside DB transactions initiated by
  trusted services, never by the game client.
- `transactions` is append-only and sufficient to replay/rollback the economy.
- User-generated content rows store a content hash + S3 ref + validation
  status; unvalidated content is never served to game servers.
