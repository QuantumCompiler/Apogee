# Graph report: work

Assembled from the store with no model call: every fact below is the graph's own, and every list is capped and says how many it had.

## Overview

- **Entities:** 73 -- artifact 1, class 1, concept 12, decision 14, file 6, function 34, name 2, organization 1, system 2
- **Relations:** 86
- **Members:** `app`, `notes`
- **Unresolved names:** 2 -- references the parsed trees do not define, left out of every ranking below

## Origin

86 relations: 69 extracted (parsed from source), 17 inferred (asserted by a model).

## Hubs

Ranked by degree -- every relation that touches an entity, either way; unresolved names left out. The top 10 of 71 entities:

1. `pkg.lib.helper` (function, `pkg/lib.py:1`) -- 18 relations (1 out, 17 in), 1 mention
    - calls <- 17: `pkg.app.run`, `pkg.callers.c01`, `pkg.callers.c02`, and 14 more
    - defined_in -> 1: `pkg/lib.py`
2. `pkg/callers.py` (file, `pkg/callers.py:1`) -- 15 relations (0 out, 15 in), 1 mention
    - defined_in <- 15: `pkg.callers.c01`, `pkg.callers.c02`, `pkg.callers.c03`, and 12 more
3. `Ledger` (artifact) -- 13 relations (0 out, 13 in), 1 mention
    - concerns <- 13: `kr-0101`, `kr-0102`, `kr-0103`, and 10 more
4. `pkg/chain.py` (file, `pkg/chain.py:1`) -- 11 relations (0 out, 11 in), 1 mention
    - defined_in <- 11: `pkg.chain.n0`, `pkg.chain.n1`, `pkg.chain.n10`, and 8 more
5. `pkg.app.run` (function, `pkg/app.py:5`) -- 6 relations (5 out, 1 in), 1 mention
    - calls -> 4: `pkg.lib.helper`, `pkg.lib.other`, `.push_back`, and 1 more
    - calls <- 1: `pkg.app.main`
    - defined_in -> 1: `pkg/app.py`
6. `pkg.lib.Store` (class, `pkg/lib.py:10`) -- 4 relations (1 out, 3 in), 1 mention
    - defined_in -> 1: `pkg/lib.py`
    - defined_in <- 2: `pkg.lib.Store.Add`, `pkg.lib.Store.add`
    - implemented by <- 1: `Vault`
7. `pkg.lib.other` (function, `pkg/lib.py:6`) -- 4 relations (3 out, 1 in), 1 mention
    - calls -> 2: `pkg.lib.helper`, `json.dumps`
    - calls <- 1: `pkg.app.run`
    - defined_in -> 1: `pkg/lib.py`
8. `pkg/lib.py` (file, `pkg/lib.py:1`) -- 4 relations (0 out, 4 in), 1 mention
    - defined_in <- 3: `pkg.lib.Store`, `pkg.lib.helper`, `pkg.lib.other`
    - imports <- 1: `pkg/app.py`
9. `Vault` (system) -- 3 relations (1 out, 2 in), 14 mentions
    - concerns <- 1: `kr-0001`
    - implemented by -> 1: `pkg.lib.Store`
    - stores readings in <- 1: `Atlas`
10. `pkg.chain.n1` (function, `pkg/chain.py:3`) -- 3 relations (2 out, 1 in), 1 mention
    - calls -> 1: `pkg.chain.n2`
    - calls <- 1: `pkg.chain.n0`
    - defined_in -> 1: `pkg/chain.py`

## Communities

1 community is stored; none has a summary -- `apogee graph communities work -m <backend>` writes them. Largest first:

### Community 2 -- 3 members

_No summary -- clustered with no model._

Members: `Vault` (system), `Atlas` (system), `kr-0001` (decision).

## Cross-collection links

Members (collections and source trees): `app`, `notes`. 1 relation joins entities stated in different members; no entity is stated in more than one.

| Members | Relations across | Entities in both |
|---|---|---|
| `app` -- `notes` | 1 | 0 |

Relations across:

- `Vault` (system; notes) -[implemented by·inferred]-> `pkg.lib.Store` (class; app)

## Decisions

14 decision records are attached. The 10 most connected:

- `kr-0001` (shipped, engineering): Keep every reading in Vault — one store, one backup
  - concerns -> 1: `Vault`
- `kr-0101` (proposed, finance): Ledger rule 1
  - concerns -> 1: `Ledger`
- `kr-0102` (proposed, finance): Ledger rule 2
  - concerns -> 1: `Ledger`
- `kr-0103` (proposed, finance): Ledger rule 3
  - concerns -> 1: `Ledger`
- `kr-0104` (proposed, finance): Ledger rule 4
  - concerns -> 1: `Ledger`
- `kr-0105` (proposed, finance): Ledger rule 5
  - concerns -> 1: `Ledger`
- `kr-0106` (proposed, finance): Ledger rule 6
  - concerns -> 1: `Ledger`
- `kr-0107` (proposed, finance): Ledger rule 7
  - concerns -> 1: `Ledger`
- `kr-0108` (proposed, finance): Ledger rule 8
  - concerns -> 1: `Ledger`
- `kr-0109` (proposed, finance): Ledger rule 9
  - concerns -> 1: `Ledger`
- and 4 more

## Orphans

12 entities have no relation at all -- worth asking about. The 10 most mentioned:

- `Orphan 12` (concept) -- 12 mentions: An orphan, mentioned 12 time(s).
- `Orphan 11` (concept) -- 11 mentions: An orphan, mentioned 11 time(s).
- `Orphan 10` (concept) -- 10 mentions: An orphan, mentioned 10 time(s).
- `Orphan 09` (concept) -- 9 mentions: An orphan, mentioned 9 time(s).
- `Orphan 08` (concept) -- 8 mentions: An orphan, mentioned 8 time(s).
- `Orphan 07` (concept) -- 7 mentions: An orphan, mentioned 7 time(s).
- `Orphan 06` (concept) -- 6 mentions: An orphan, mentioned 6 time(s).
- `Orphan 05` (concept) -- 5 mentions: An orphan, mentioned 5 time(s).
- `Orphan 04` (concept) -- 4 mentions: An orphan, mentioned 4 time(s).
- `Orphan 03` (concept) -- 3 mentions: An orphan, mentioned 3 time(s).

## Summary

Graph "work" holds 73 entities and 86 relations across 2 members (app, notes). 69 were parsed from source and 17 asserted by a model. Its busiest entity is pkg.lib.helper (function), with 18 relations. 1 community is stored; none has a summary. 1 relation crosses between members. 14 decision records are attached. 12 entities stand alone, with no relation.
