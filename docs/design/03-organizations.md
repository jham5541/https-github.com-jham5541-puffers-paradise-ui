# Organizations

Players can form several organization types. All share one underlying system:
membership, roles, permissions, shared assets, shared accounts, and audit logs.

## Teams
Temporary cooperative groups for missions, races, heists, events, exploration.
No persistence beyond the activity.

## Clubs
Social organizations with: clubhouse, membership tiers, private events, shared
storage, club colors, club vehicles, internal chat, member dues.

## Gangs
Territory-based criminal organizations with: rank hierarchy, turf ownership,
reputation, rivalries, safehouses, illegal businesses, protection income, gang
missions, shared weapon storage, custom clothing and symbols.

> Gang content must use **original names and iconography** — no real-world gang
> names, signs, or colors.

## Businesses
Player-created companies with: owners, employees, departments, payroll,
scheduling, inventory, revenue, expenses, contracts, taxes, customer ratings,
business licenses, shared accounts, physical property, advertising.

## Custom roles and permissions

Players define roles such as: owner, president, vice president, manager,
treasurer, recruiter, security, driver, salesperson, employee, prospect,
associate. Each role receives custom permissions (invite, kick, withdraw funds,
set prices, access storage, manage property, start org missions, etc.).

Permission checks are **always server-side**, and every privileged action
(withdrawals, evictions, sales) is written to the organization audit log.
