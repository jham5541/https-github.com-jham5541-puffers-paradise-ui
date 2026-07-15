import type { FastifyInstance } from "fastify";
import { z } from "zod";
import { pool, withTransaction } from "../db.js";

const CreateCharacter = z.object({
  accountId: z.string().uuid(),
  name: z.string().min(2).max(32),
  appearance: z.record(z.unknown()).default({}),
});

export async function characterRoutes(app: FastifyInstance) {
  app.post("/", async (req, reply) => {
    const body = CreateCharacter.parse(req.body);

    const character = await withTransaction(async (tx) => {
      const { rows } = await tx.query(
        `INSERT INTO characters (account_id, name, appearance)
         VALUES ($1, $2, $3) RETURNING id, name, created_at`,
        [body.accountId, body.name, JSON.stringify(body.appearance)],
      );
      await tx.query(
        `INSERT INTO wallets (character_id, balance_cents) VALUES ($1, 0)`,
        [rows[0].id],
      );
      return rows[0];
    });

    return reply.code(201).send(character);
  });

  app.get("/:id", async (req, reply) => {
    const { id } = z.object({ id: z.string().uuid() }).parse(req.params);
    const { rows } = await pool.query(
      `SELECT c.id, c.name, c.appearance, c.skills, c.reputation, w.balance_cents
       FROM characters c JOIN wallets w ON w.character_id = c.id
       WHERE c.id = $1`,
      [id],
    );
    if (rows.length === 0) return reply.code(404).send({ error: "not_found" });
    return rows[0];
  });
}
