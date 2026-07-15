import type { FastifyInstance } from "fastify";
import { z } from "zod";
import { pool, withTransaction } from "../db.js";

const ZONES = ["residential", "commercial", "industrial", "mixed"] as const;

const BUILDING_CATEGORIES = [
  // MVP buildable set — expands per docs/design/02-construction.md
  "house",
  "store",
  "office",
  "gang_hq",
] as const;

const PurchaseParcel = z.object({
  district: z.string().min(1),
  zoning: z.enum(ZONES),
  buyerCharacterId: z.string().uuid(),
  priceCents: z.number().int().positive(),
});

const SubmitBuilding = z.object({
  category: z.enum(BUILDING_CATEGORIES),
  layoutRef: z.string().min(1),  // S3 key of the modular layout document
  layoutHash: z.string().min(1),
  occupancyLimit: z.number().int().min(1).max(64).default(8),
});

const CreateLease = z.object({
  tenantCharacterId: z.string().uuid(),
  rentCents: z.number().int().min(0),
  period: z.enum(["weekly", "monthly"]).default("weekly"),
  depositCents: z.number().int().min(0).default(0),
  terms: z.record(z.unknown()).default({}),
});

export async function propertyRoutes(app: FastifyInstance) {
  // Buy a parcel: wallet debit + ownership + ledger row, atomically.
  app.post("/parcels/purchase", async (req, reply) => {
    const body = PurchaseParcel.parse(req.body);

    try {
      const parcel = await withTransaction(async (tx) => {
        const { rowCount } = await tx.query(
          `UPDATE wallets SET balance_cents = balance_cents - $1
           WHERE character_id = $2 AND balance_cents >= $1`,
          [body.priceCents, body.buyerCharacterId],
        );
        if (rowCount === 0) throw new Error("insufficient_funds");

        const { rows } = await tx.query(
          `INSERT INTO parcels (district, zoning, owner_character)
           VALUES ($1, $2, $3) RETURNING id, district, zoning`,
          [body.district, body.zoning, body.buyerCharacterId],
        );
        await tx.query(
          `INSERT INTO transactions (kind, debit_wallet, credit_wallet, amount_cents, context)
           VALUES ('parcel_purchase', $1, NULL, $2, $3)`,
          [body.buyerCharacterId, body.priceCents, JSON.stringify({ parcelId: rows[0].id })],
        );
        return rows[0];
      });
      return reply.code(201).send(parcel);
    } catch (err) {
      if (err instanceof Error && err.message === "insufficient_funds") {
        return reply.code(409).send({ error: "insufficient_funds" });
      }
      throw err;
    }
  });

  // Submit a building for validation. Zoning is checked here; geometry,
  // collision, navmesh, and perf validation run in the async pipeline that
  // flips validation to 'active' or 'rejected'.
  app.post("/parcels/:parcelId/buildings", async (req, reply) => {
    const { parcelId } = z.object({ parcelId: z.string().uuid() }).parse(req.params);
    const body = SubmitBuilding.parse(req.body);

    const zoningByCategory: Record<string, (typeof ZONES)[number][]> = {
      house: ["residential", "mixed"],
      store: ["commercial", "mixed"],
      office: ["commercial", "mixed"],
      gang_hq: ["industrial", "mixed"],
    };

    const { rows: parcels } = await pool.query(
      `SELECT zoning FROM parcels WHERE id = $1`,
      [parcelId],
    );
    if (parcels.length === 0) return reply.code(404).send({ error: "parcel_not_found" });
    if (!zoningByCategory[body.category].includes(parcels[0].zoning)) {
      return reply.code(409).send({ error: "zoning_violation", zoning: parcels[0].zoning });
    }

    const { rows } = await pool.query(
      `INSERT INTO buildings (parcel_id, category, layout_ref, layout_hash, occupancy_limit)
       VALUES ($1, $2, $3, $4, $5)
       RETURNING id, category, validation`,
      [parcelId, body.category, body.layoutRef, body.layoutHash, body.occupancyLimit],
    );
    return reply.code(201).send(rows[0]);
  });

  // Create a unit + lease on an active building.
  app.post("/buildings/:buildingId/leases", async (req, reply) => {
    const { buildingId } = z.object({ buildingId: z.string().uuid() }).parse(req.params);
    const body = CreateLease.parse(req.body);

    const { rows: buildings } = await pool.query(
      `SELECT validation FROM buildings WHERE id = $1`,
      [buildingId],
    );
    if (buildings.length === 0) return reply.code(404).send({ error: "building_not_found" });
    if (buildings[0].validation !== "active") {
      return reply.code(409).send({ error: "building_not_active" });
    }

    const lease = await withTransaction(async (tx) => {
      const { rows: units } = await tx.query(
        `INSERT INTO units (building_id, label) VALUES ($1, 'Unit') RETURNING id`,
        [buildingId],
      );
      const { rows } = await tx.query(
        `INSERT INTO leases (unit_id, tenant, rent_cents, period, deposit_cents, terms)
         VALUES ($1, $2, $3, $4, $5, $6)
         RETURNING id, unit_id, rent_cents, period, status`,
        [units[0].id, body.tenantCharacterId, body.rentCents, body.period,
         body.depositCents, JSON.stringify(body.terms)],
      );
      return rows[0];
    });
    return reply.code(201).send(lease);
  });
}
