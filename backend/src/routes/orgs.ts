import type { FastifyInstance } from "fastify";
import { z } from "zod";
import { pool, withTransaction } from "../db.js";

const ORG_TYPES = ["team", "club", "gang", "business", "political"] as const;

// Permission bitset — must stay in sync with the game's org permission enum.
export const OrgPermission = {
  Invite: 1n << 0n,
  Kick: 1n << 1n,
  WithdrawFunds: 1n << 2n,
  ManageRoles: 1n << 3n,
  ManageProperty: 1n << 4n,
  AccessStorage: 1n << 5n,
  SetPrices: 1n << 6n,
  StartOrgMissions: 1n << 7n,
} as const;

const OWNER_PERMISSIONS = Object.values(OrgPermission).reduce((a, b) => a | b, 0n);

const CreateOrg = z.object({
  orgType: z.enum(ORG_TYPES),
  name: z.string().min(2).max(48),
  founderCharacterId: z.string().uuid(),
  charter: z.record(z.unknown()).default({}),
});

const AddMember = z.object({
  characterId: z.string().uuid(),
  roleName: z.string().min(1).max(32),
});

export async function orgRoutes(app: FastifyInstance) {
  // Create an organization; founder gets an Owner role with all permissions.
  app.post("/", async (req, reply) => {
    const body = CreateOrg.parse(req.body);

    const org = await withTransaction(async (tx) => {
      const { rows: orgRows } = await tx.query(
        `INSERT INTO organizations (org_type, name, charter)
         VALUES ($1, $2, $3) RETURNING id, org_type, name, created_at`,
        [body.orgType, body.name, JSON.stringify(body.charter)],
      );
      const { rows: roleRows } = await tx.query(
        `INSERT INTO org_roles (org_id, name, permissions)
         VALUES ($1, 'Owner', $2) RETURNING id`,
        [orgRows[0].id, OWNER_PERMISSIONS.toString()],
      );
      await tx.query(
        `INSERT INTO org_members (org_id, character_id, role_id)
         VALUES ($1, $2, $3)`,
        [orgRows[0].id, body.founderCharacterId, roleRows[0].id],
      );
      return orgRows[0];
    });

    return reply.code(201).send(org);
  });

  // Add a member with a named role (role must already exist on the org).
  app.post("/:orgId/members", async (req, reply) => {
    const { orgId } = z.object({ orgId: z.string().uuid() }).parse(req.params);
    const body = AddMember.parse(req.body);

    const { rows: roles } = await pool.query(
      `SELECT id FROM org_roles WHERE org_id = $1 AND name = $2`,
      [orgId, body.roleName],
    );
    if (roles.length === 0) {
      return reply.code(404).send({ error: "role_not_found" });
    }

    await pool.query(
      `INSERT INTO org_members (org_id, character_id, role_id)
       VALUES ($1, $2, $3)
       ON CONFLICT (org_id, character_id) DO UPDATE SET role_id = EXCLUDED.role_id`,
      [orgId, body.characterId, roles[0].id],
    );
    return reply.code(201).send({ ok: true });
  });

  app.get("/:orgId", async (req, reply) => {
    const { orgId } = z.object({ orgId: z.string().uuid() }).parse(req.params);
    const { rows } = await pool.query(
      `SELECT o.id, o.org_type, o.name, o.charter, o.treasury_cents,
              COUNT(m.character_id)::int AS member_count
       FROM organizations o
       LEFT JOIN org_members m ON m.org_id = o.id
       WHERE o.id = $1
       GROUP BY o.id`,
      [orgId],
    );
    if (rows.length === 0) return reply.code(404).send({ error: "not_found" });
    return rows[0];
  });
}
