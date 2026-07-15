# Backend Services

Platform services behind the UE5 dedicated servers. Game clients **never**
talk to these directly except for auth/session bootstrap — game servers are
the trusted callers.

## Stack

- Node.js 20+ / TypeScript
- PostgreSQL (permanent game data)
- Redis (sessions, matchmaking, cache)
- S3-compatible storage (user-generated content) — later phase

## Services (single process for now, split later)

| Route prefix | Service | Phase |
|---|---|---|
| `/v1/auth` | Session bootstrap (EOS/Steam ticket exchange) | 4 |
| `/v1/characters` | Persistent character state | 2–4 |
| `/v1/economy` | Wallets + append-only transaction ledger | 2 |
| `/v1/orgs` | Organizations, roles, permissions | 2–3 |
| `/v1/property` | Parcels, buildings, leases | 3 |
| `/v1/servers` | Player-server templates and configs | 4 |

## Run locally

```bash
cd backend
npm install
docker compose up -d   # postgres + redis
npm run dev
```

Schema lives in `db/schema.sql` and is applied automatically by compose on
first boot.
