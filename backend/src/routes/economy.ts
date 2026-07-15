import type { FastifyInstance } from "fastify";
import { z } from "zod";
import { withTransaction } from "../db.js";

const Transfer = z.object({
  kind: z.string().min(1),
  debitWallet: z.string().uuid().nullable(),   // null = money created (mission reward)
  creditWallet: z.string().uuid().nullable(),  // null = money destroyed (tax, fee)
  amountCents: z.number().int().positive(),
  context: z.record(z.unknown()).default({}),
});

/**
 * The only way money moves. Every call writes the append-only ledger row
 * inside the same DB transaction as the balance updates, so the economy can
 * always be audited and rolled back.
 */
export async function economyRoutes(app: FastifyInstance) {
  app.post("/transfer", async (req, reply) => {
    const t = Transfer.parse(req.body);
    if (!t.debitWallet && !t.creditWallet) {
      return reply.code(400).send({ error: "no_parties" });
    }

    try {
      const result = await withTransaction(async (tx) => {
        if (t.debitWallet) {
          const { rowCount } = await tx.query(
            `UPDATE wallets SET balance_cents = balance_cents - $1
             WHERE character_id = $2 AND balance_cents >= $1`,
            [t.amountCents, t.debitWallet],
          );
          if (rowCount === 0) throw new Error("insufficient_funds");
        }
        if (t.creditWallet) {
          await tx.query(
            `UPDATE wallets SET balance_cents = balance_cents + $1
             WHERE character_id = $2`,
            [t.amountCents, t.creditWallet],
          );
        }
        const { rows } = await tx.query(
          `INSERT INTO transactions (kind, debit_wallet, credit_wallet, amount_cents, context)
           VALUES ($1, $2, $3, $4, $5) RETURNING id, occurred_at`,
          [t.kind, t.debitWallet, t.creditWallet, t.amountCents, JSON.stringify(t.context)],
        );
        return rows[0];
      });
      return reply.code(201).send(result);
    } catch (err) {
      if (err instanceof Error && err.message === "insufficient_funds") {
        return reply.code(409).send({ error: "insufficient_funds" });
      }
      throw err;
    }
  });
}
