# World Structure

Do not start with a full country-sized map. Start with **one dense city**,
divided into streaming cells (UE5 World Partition) so only nearby areas load.

## Initial city districts

- Downtown business district
- Low-income residential district
- Luxury hillside district
- Industrial port
- Beachfront entertainment district
- Suburban outskirts
- Airport
- Rural edge
- Construction zone
- Underground tunnels
- Police, fire, and medical facilities
- Clubs, stores, dealerships, offices, apartments, warehouses

## Enterable buildings — three categories

### 1. Fully simulated buildings
Complete interiors streamed with the world. Reserved for important locations:
player homes, businesses, clubs, warehouses, police stations, hospitals, banks,
mission locations, government buildings, hotels, apartments.

### 2. Instanced interiors
The building appears enterable, but the interior loads as a separate instance
(level streamed or seamless-travel sublevel). Efficient for: apartments,
offices, nightclubs, stores, garages, rental properties, hotels.

### 3. Procedural interiors
The player chooses a layout template, size, room configuration, and style; the
game assembles the interior from modular pieces. Used for player-constructed
buildings and bulk rental stock.

## Streaming rules

- World Partition grid sized so a fast vehicle never outruns streaming.
- Interiors of category 2/3 never count against exterior cell budgets.
- Every district carries a **server performance rating** budget that
  player-built structures draw down from (see 02-construction.md).
