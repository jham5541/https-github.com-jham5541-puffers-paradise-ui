# Career systems (handoff Rev 12)

Every mechanic from the Rev 12 `CLAUDE_HANDOFF.md`, built as one game-instance subsystem, `UPuffersCareerSubsystem`,
with one save (`PuffersCareer0`). Because there's one owner and one save, a multi-system settlement (a raid, a strike,
an as-built sale) commits all at once. All tuning lives in `Content/Data/Career/*.json`.

Jayde's decisions that override the doc:
- Raids can take the whole uninsured estate, owned homes included.
- Outskirts plot fees stay real-money monthly subscriptions (a server upcharge). Non-payment leads to a forced sale as built.
- The care window is 7 days (`CareWindowHours` 168).
- Offline play is story missions only.
- The counties are Central, Heights, Southside, Frontier and Coast.

## Systems

| System | What it does | Code |
|---|---|---|
| Wallet | Double-entry Puff Cash: Pocket, Bank and `Reserve.<business>` accounts, holds, receipts and reversals. Every tx id is idempotent. Pocket mirrors the existing mission wallet. | `PuffersLedger`, subsystem wallet API |
| Route and licensing | Starts Uncommitted, then becomes LicensedOnly, StreetDualEligible or StreetForfeited. A street operation is a permanent commitment. The first confirmed, authoritative street strike revokes every cannabis license, closes regulated sales, releases prepaid sales and bars new licenses. A mistaken-identity correction restores eligibility, but only when no other strike remains. Non-cannabis businesses and jobs never get hit. | `PuffersCareerRules` |
| Operations | Archetypes cover licensed shops and grows, street ops, and non-cannabis businesses (restaurant, bodega, pet groomer, garage, nightclub, merch). Each has an entry cost, fixtures, staff, profitable-week streaks and 8 career phases (First paycheck to Legacy). | `Career.json` |
| County law | Each county's law is versioned, with a seed fine-only cap of 3500 mg. Templates cover the 6 lb reform and a 5-year firearm minimum. A violence spike opens a firearm review automatically. Proposals go through support, review and an NPC or server judge, then get scheduled and published. Cases use the law in force at the time of the conduct. Convictions become custody blocks at the persisted custody scale, which feeds the sentence service (`CUS_ROUTINE`). Bribes resolve from a stable roll and never re-roll. | `PuffersCountyLawRules`, `CountyLaws.json` |
| Insiders | Lawyers, county officials and officers. Covers quotes (accept, counter, refuse or report), fee holds that are charged only on success, gifts, consent-based intimacy, what they know, promises, grievances, fallout (clean split, withdraw, leak or formal report) and "observe and report" jobs. | `PuffersInsiderRules`, `Insiders.json` |
| Care and estate | The online career has to clock in and do a care activity every 7 days. Past the deadline, an uninsured owner with a residence can be raided. One raid runs at a time. Care closes a raid. Success transfers the whole eligible estate exactly once: cash, items, vehicles, plots, lease income and business interests. Liened and frozen assets stay. Anything beyond the winner's 30 carry slots goes to claim storage. Life insurance covers yourself (raid protection), a team member or a significant other. | `PuffersEstateRules` |
| Housing | City apartments are monthly subscriptions with mock billing. A missed renewal locks the unit, renewing reinstates it, and after a 30-day grace the contents go back to the marketplace. Business vacancies use applications with zoning and license checks and a 48-hour reservation. Outskirts plots carry buildings and player leases. A lapsed plot with a finished building is listed as built: liens are paid off first, tenants keep their leases, and movable items go back to the owner. | `PuffersHousingRules`, `Housing.json` |
| Loans | Loans are secured by vehicles, homes or jewelry, at loan-to-value times condition times ownership share. Rented homes and plots without transferable rights are refused. A lien blocks transfers. After a missed installment plus the grace period, the lender repossesses. | `PuffersWorkforceRules`, `Career.json` Lenders |
| Payroll | Hiring needs available cash of at least max(500, a week of projected payroll), and no unpaid wages. AI and player shifts can be accepted, checked in (which locks the wage), completed or substituted. The weekly pay run happens once per cutoff and pays from the business reserve, then Pocket. A shortfall becomes arrears, sends the alert "Your boss can't pay you this week. You're owed X Puff Cash.", and lets the worker choose to wait, quit or rob the boss. | subsystem payroll API |

## Missions: "Earn your first operation" (`Content/Data/Missions/FirstOperation.json`)

| Id | Mission | Career effect (via `Hooks.json`) |
|---|---|---|
| FO01 | First Paycheck (replayable) | Clock in, then get paid 220 per shift. Counts as care. |
| FO02 | Pick Your Lane | Choose a licensed shop (1,500), a street corner (800, a permanent commitment) or a bodega (900). Then fixtures, open, and the first sale. |
| FO03 | Corner Dispute | Walk away, pay 200, or fight. Fighting is a violent incident that feeds the firearm review. |
| FO04 | Wrong Face | An outfit-match finding (not a strike), then cleared by the lawyer or the footage. |
| FO05 | Off the Books | Selling counts as a verified strike, which revokes licenses, plus a possession case. |
| FO06 | Back to Zero (replayable) | The recovery job (SHOP_STAFF) and the RecoveryJob milestone. |
| FO07 | County Hearing | The six-pound reform: draft, three support sources, review, and the judge. It needs civic access (2 profitable weeks and 2,000 net). |

LC05 "Your Name on the Door" goes through the license gate when it starts, and opens Lato's shop when it completes.

All 39 objectives resolve to real step gameplay (checked with `python3 Scripts/step_coverage.py --missions Content/Data/Missions`).

## Hook actions (`Verb:arg:arg`)

`PayShift:wage:job`, `Wallet:amount:memo`, `CommitStreet`, `StartOperation:archetype:opId:county[:free]`,
`InstallAllFixtures:op`, `OpenOperation:op`, `CloseOperation:op`, `Sale:op:amount`,
`Finding:Kind:confirmed(0/1):authoritative(0/1)[:county]`, `CorrectLastFinding[:reason]`, `Incident:county:weight`,
`Propose:template:county`, `Support:county:amount`, `Review:county`, `Judge:county`, `PublishNow:county`,
`Case:county:mg:firearm(0/1):courtYears`, `ClockIn`, `Care:activity`, `Milestone:Name`, `Notice:text`,
`GoOnline`, `Apartment:unit`, `Deposit:business:amount`, `Hire:business:worker:wage:shiftsPerWeek[:ai|player]`,
`Payday:business`, `SettleWages:business`.

Hook events: `ObjectiveDone` (context: objective id), `MissionStarted` and `MissionCompleted` (context: mission id), or any game event name.

## Modes

- **Offline (story):** route, licensing, county law and insiders work with the same rules. Operations, housing, care, raids, loans and payroll are refused with "This is part of the online world."
- **Online:** the server is the authority (`NetMode != Client`).
- **Development:** `puffers.Career.LocalAuthority 1` (the default outside Shipping) lets a standalone Test Yard act as its own server.

## Console (development builds)

- `puffers.Career.Status`
- `puffers.Career.Checklist`
- `puffers.Career.Do <Action>`, for example `puffers.Career.Do StartOperation:BODEGA:OP_TEST:Central`
- `puffers.Career.Law <County>`
- `puffers.Career.Manifest`
- `puffers.Career.Reload`

## Tests

`Puffers.Career.*` has 20 automation tests. They cover:
- the data checks
- the ledger
- one strike and mistaken identity
- the law caps at 3500/3501 mg and 6 lb/+1 mg
- the firearm minimum, the reform flow and bribe determinism
- the 7-day care boundary, self cover and claims
- raids (one at a time, care closes them, they settle once)
- apartment lock, reinstate and reclaim
- the vacancy race
- an as-built sale with a lien and a tenant
- loans and repossession
- the hiring floor, unpaid payroll and worker choices
- the mission hooks and offline mode

## Integration notes and follow-ups

- **Map counties:** the five law-map slices (Westgate, NorthPoint, EastSide, Ironwood, SouthHarbor) are mapped to Central, Heights, Southside, Frontier and Coast through `MapCountyIds`. Re-slice the map when the world plan's county borders are placed.
- **Vehicles:** the garage has no title-transfer API yet. `GetOwnedVehicles` and `TransferVehicle` are unbound, so raids and repossession skip vehicles until the garage exposes them.
- **Billing:** apartment and plot billing is mocked through `ApplyBillingEvent`. Wire it to the real subscription webhooks when the backend exists.
- **Rent:** the existing `PropertySubsystem` Puff Cash apartment rent is unchanged. The subscription path is separate, and the two should be merged when the property UI is reworked.
- **UI:** none yet. Blueprint-callable functions and the notices (`OnNotice`) feed the phone UI.
- **Compile:** not yet compiled. The cloud can't build UE. The first compile happens on the Mac once the "Give all missions real gameplay" build is finished.
