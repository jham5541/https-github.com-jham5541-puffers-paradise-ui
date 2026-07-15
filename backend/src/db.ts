import pg from "pg";

export const pool = new pg.Pool({
  connectionString:
    process.env.DATABASE_URL ??
    "postgres://puffers:puffers_dev_only@localhost:5432/empire_city",
});

/** Run a callback inside a DB transaction (money/ownership must use this). */
export async function withTransaction<T>(
  fn: (client: pg.PoolClient) => Promise<T>,
): Promise<T> {
  const client = await pool.connect();
  try {
    await client.query("BEGIN");
    const result = await fn(client);
    await client.query("COMMIT");
    return result;
  } catch (err) {
    await client.query("ROLLBACK");
    throw err;
  } finally {
    client.release();
  }
}
