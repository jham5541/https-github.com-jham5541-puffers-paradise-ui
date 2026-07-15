-- The Puffers Club — initial persistent data model.
-- Source of truth for money/ownership. Game clients never connect here;
-- only trusted services do. See docs/tech/data-model.md.

CREATE TABLE accounts (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    platform        TEXT NOT NULL,                -- 'steam' | 'eos' | 'apple'
    platform_id     TEXT NOT NULL,
    standing        TEXT NOT NULL DEFAULT 'good', -- 'good' | 'suspended' | 'banned'
    created_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE (platform, platform_id)
);

CREATE TABLE characters (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    account_id      UUID NOT NULL REFERENCES accounts(id),
    name            TEXT NOT NULL,
    appearance      JSONB NOT NULL DEFAULT '{}'::jsonb,
    skills          JSONB NOT NULL DEFAULT '{}'::jsonb,
    reputation      JSONB NOT NULL DEFAULT '{}'::jsonb,
    criminal_record JSONB NOT NULL DEFAULT '[]'::jsonb,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE wallets (
    character_id    UUID PRIMARY KEY REFERENCES characters(id),
    balance_cents   BIGINT NOT NULL DEFAULT 0 CHECK (balance_cents >= 0)
);

-- Append-only ledger: every money/ownership mutation. Never UPDATE/DELETE.
CREATE TABLE transactions (
    id              BIGSERIAL PRIMARY KEY,
    occurred_at     TIMESTAMPTZ NOT NULL DEFAULT now(),
    kind            TEXT NOT NULL,        -- 'mission_reward' | 'purchase' | 'rent' | ...
    debit_wallet    UUID REFERENCES wallets(character_id),
    credit_wallet   UUID REFERENCES wallets(character_id),
    amount_cents    BIGINT NOT NULL CHECK (amount_cents > 0),
    context         JSONB NOT NULL DEFAULT '{}'::jsonb
);

CREATE TABLE organizations (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    org_type        TEXT NOT NULL,        -- 'team' | 'club' | 'gang' | 'business' | 'political'
    name            TEXT NOT NULL UNIQUE,
    charter         JSONB NOT NULL DEFAULT '{}'::jsonb,
    treasury_cents  BIGINT NOT NULL DEFAULT 0 CHECK (treasury_cents >= 0),
    created_at      TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE org_roles (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    org_id          UUID NOT NULL REFERENCES organizations(id),
    name            TEXT NOT NULL,
    permissions     BIGINT NOT NULL DEFAULT 0,   -- permission bitset
    UNIQUE (org_id, name)
);

CREATE TABLE org_members (
    org_id          UUID NOT NULL REFERENCES organizations(id),
    character_id    UUID NOT NULL REFERENCES characters(id),
    role_id         UUID NOT NULL REFERENCES org_roles(id),
    joined_at       TIMESTAMPTZ NOT NULL DEFAULT now(),
    PRIMARY KEY (org_id, character_id)
);

CREATE TABLE parcels (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    district        TEXT NOT NULL,
    zoning          TEXT NOT NULL,        -- 'residential' | 'commercial' | 'industrial' | ...
    owner_character UUID REFERENCES characters(id),
    owner_org       UUID REFERENCES organizations(id),
    CHECK (owner_character IS NULL OR owner_org IS NULL)
);

CREATE TABLE buildings (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    parcel_id       UUID NOT NULL REFERENCES parcels(id),
    category        TEXT NOT NULL,        -- 'house' | 'store' | 'office' | 'gang_hq' | ...
    layout_ref      TEXT,                 -- S3 key of validated modular layout
    layout_hash     TEXT,
    validation      TEXT NOT NULL DEFAULT 'pending',  -- 'pending' | 'active' | 'rejected'
    perf_rating     INT NOT NULL DEFAULT 0,
    occupancy_limit INT NOT NULL DEFAULT 8,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE units (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    building_id     UUID NOT NULL REFERENCES buildings(id),
    label           TEXT NOT NULL
);

CREATE TABLE leases (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    unit_id         UUID NOT NULL REFERENCES units(id),
    tenant          UUID NOT NULL REFERENCES characters(id),
    rent_cents      BIGINT NOT NULL CHECK (rent_cents >= 0),
    period          TEXT NOT NULL DEFAULT 'weekly',   -- 'weekly' | 'monthly'
    deposit_cents   BIGINT NOT NULL DEFAULT 0,
    terms           JSONB NOT NULL DEFAULT '{}'::jsonb,
    status          TEXT NOT NULL DEFAULT 'active',   -- 'active' | 'ended' | 'evicted'
    started_at      TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE vehicles (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    owner           UUID NOT NULL REFERENCES characters(id),
    model           TEXT NOT NULL,        -- fictional manufacturer/model id
    plate           TEXT,
    insured         BOOLEAN NOT NULL DEFAULT false,
    mods            JSONB NOT NULL DEFAULT '{}'::jsonb,
    stored_at       UUID REFERENCES buildings(id)
);

CREATE INDEX idx_transactions_occurred ON transactions (occurred_at);
CREATE INDEX idx_characters_account ON characters (account_id);
CREATE INDEX idx_buildings_parcel ON buildings (parcel_id);
