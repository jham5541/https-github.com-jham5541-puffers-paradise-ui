import Fastify from "fastify";
import { economyRoutes } from "./routes/economy.js";
import { characterRoutes } from "./routes/characters.js";
import { orgRoutes } from "./routes/orgs.js";
import { propertyRoutes } from "./routes/property.js";

const app = Fastify({ logger: true });

app.get("/healthz", async () => ({ ok: true, service: "puffersclub-backend" }));

await app.register(characterRoutes, { prefix: "/v1/characters" });
await app.register(economyRoutes, { prefix: "/v1/economy" });
await app.register(orgRoutes, { prefix: "/v1/orgs" });
await app.register(propertyRoutes, { prefix: "/v1/property" });

const port = Number(process.env.PORT ?? 8080);
await app.listen({ port, host: "0.0.0.0" });
