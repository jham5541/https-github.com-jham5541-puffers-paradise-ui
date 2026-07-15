# Economy and Persistent Data

The economy is **server-authoritative**. Clients never compute money.

## Currency sources (money in)

Missions, jobs, businesses, sales, rentals, contracts, events, trading.

## Currency sinks (money out)

Property costs, rent, taxes, repairs, fuel, insurance, payroll, medical bills,
fines, licenses, construction, inventory, server fees.

## Anti-inflation systems

Property taxes, maintenance, business operating costs, vehicle depreciation,
insurance, dynamic pricing, transaction fees, limited zoning, construction
costs, licensing, luxury asset upkeep.

## Monetization boundaries

No direct competitive power for money. Safe categories: cosmetics, server
subscriptions, building themes, optional content packs, convenience features.
Platform commerce (Steam/Epic/Apple) for platform purchases; Stripe only for
web purchases where platform rules permit.

## Persistent player data

Stored per account: account, character(s), inventory, vehicles, properties,
bank accounts, businesses, organization membership, mission state, criminal
record, licenses, skills, reputation, rental agreements, building data, server
memberships, moderation history.

## Transaction logging

Every important action (money movement, ownership transfer, construction
placement, org permission change) writes an append-only transaction log entry
so damaged economies can be **rolled back**. This is non-negotiable from the
first prototype that has money in it.
