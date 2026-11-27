import type { FastifyInstance, FastifyReply, FastifyRequest } from "fastify";
import { z } from "zod";
import { pool } from "../db.js";

// Player builds on the world map. Game servers POST changes and poll GET ?since=<seq> so builds made on
// any server show up on every server within a few seconds. Only game servers may write (shared key).

const STATES = ["UnderConstruction", "Completed", "ForSale", "Raided", "Demolished"] as const;

const Build = z.object({
  buildId: z.string().min(1).max(160),
  plotId: z.string().max(80).default(""),
  ownerId: z.string().min(1).max(128),
  ownerName: z.string().max(40).default(""),
  use: z.string().max(32).default(""),
  label: z.string().max(64).default(""),
  state: z.enum(STATES).default("Completed"),
  // Map meters, flat [x0, y0, x1, y1, ...], 3..64 points inside the world box (with margin).
  footprint: z.array(z.number().finite().min(-5000).max(30000)).min(6).max(128)
    .refine((a) => a.length % 2 === 0, "footprint needs x,y pairs"),
  heightM: z.number().min(0).max(400).default(0),
  version: z.number().int().min(1).default(1),
});

const PostBuilds = z.object({ builds: z.array(Build).min(1).max(200) });
const Since = z.object({
  since: z.coerce.number().int().min(0).default(0),
  limit: z.coerce.number().int().min(1).max(5000).default(2000),
});

function serverOnly(req: FastifyRequest, reply: FastifyReply): boolean {
  const key = process.env.MAP_SERVER_KEY;
  if (!key || req.headers["x-puffers-server-key"] !== key) {
    reply.code(401).send({ error: "server_key_required" });
    return false;
  }
  return true;
}

function area(p: number[]): number {
  let a = 0;
  for (let i = 0, j = p.length - 2; i < p.length; j = i, i += 2) a += p[j] * p[i + 1] - p[i] * p[j + 1];
  return Math.abs(a / 2);
}

function toJson(r: any) {
  return {
    buildId: r.build_id, plotId: r.plot_id, ownerId: r.owner_id, ownerName: r.owner_name, use: r.use,
    label: r.label, state: r.state, footprint: r.footprint, heightM: r.height_m, version: r.version,
    seq: Number(r.seq), updatedAt: Math.floor(new Date(r.updated_at).getTime() / 1000),
  };
}

export async function mapRoutes(app: FastifyInstance) {
  // Changes since a sequence number (0 = everything). Servers keep the returned cursor.
  app.get("/builds", async (req, reply) => {
    if (!serverOnly(req, reply)) return;
    const { since, limit } = Since.parse(req.query);
    const { rows } = await pool.query(
      `SELECT * FROM map_builds WHERE seq > $1 ORDER BY seq ASC LIMIT $2`, [since, limit + 1]);
    const more = rows.length > limit;
    const page = rows.slice(0, limit).map(toJson);
    const cursor = page.length ? page[page.length - 1].seq : since;
    return { builds: page, cursor, more };
  });

  // Upsert builds. An owner can't take over someone else's live build; each write bumps seq.
  app.post("/builds", async (req, reply) => {
    if (!serverOnly(req, reply)) return;
    const { builds } = PostBuilds.parse(req.body);
    const stored = [];
    const rejected = [];
    for (const b of builds) {
      if (area(b.footprint) > 40000) { rejected.push({ buildId: b.buildId, error: "footprint_too_big" }); continue; }
      const { rows } = await pool.query(
        `INSERT INTO map_builds (build_id, plot_id, owner_id, owner_name, use, label, state, footprint, height_m, version)
         VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10)
         ON CONFLICT (build_id) DO UPDATE SET
           plot_id = EXCLUDED.plot_id, owner_id = EXCLUDED.owner_id, owner_name = EXCLUDED.owner_name,
           use = EXCLUDED.use, label = EXCLUDED.label, state = EXCLUDED.state, footprint = EXCLUDED.footprint,
           height_m = EXCLUDED.height_m, version = EXCLUDED.version,
           seq = nextval('map_build_seq'), updated_at = now()
         WHERE map_builds.owner_id = EXCLUDED.owner_id OR map_builds.state = 'Demolished'
         RETURNING *`,
        [b.buildId, b.plotId, b.ownerId, b.ownerName, b.use, b.label, b.state, JSON.stringify(b.footprint), b.heightM, b.version]);
      if (rows.length) stored.push(toJson(rows[0]));
      else rejected.push({ buildId: b.buildId, error: "owned_by_another_player" });
    }
    return reply.code(200).send({ builds: stored, rejected });
  });
}
