# Criteria (instantiated) — windbreak

matrix 222 · wave spec `wave18/222-windbreak.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - (critical) "this crossing is allowed" by docs/REGISTRY.md  | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - (critical) "within budget" (ticks and bytes used against t | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - process table (program x layer, budget, capabilities) in A | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - rule-firing counts per rule as fixed records refusing on o | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - rule-firing counts per rule as fixed records refusing on o | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| - quarantine records (item, desk, verdict, evidence) in MUMP | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| the test asserts state 3, the item stays held in the Cage, a | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - an item's clearance joins its checks with `lpres_conjoin` in C (kernel order). docs/CAGE.md says "Belnap conjunction"
- CLAUDE.md says kernel order, not Belnap: use `lpres_conjoin` and report the conflict
- Paraconsistent (BOTH test, in test_zxapp.c): check A clears an item (TRUE) and check B refuses it (FALSE), giving BOTH
- never re-derive the tables
