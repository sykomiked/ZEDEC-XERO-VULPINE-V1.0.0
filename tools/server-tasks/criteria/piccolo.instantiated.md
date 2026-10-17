# Criteria (instantiated) — piccolo

matrix 385 · wave spec `wave32/385-piccolo.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `spray_ok`: (critical) pesticide limits (none while a decl | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| label rate otherwise) in Ada as Eve + APL as KRON witnessed | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| label rate otherwise) in Ada as Eve + APL as KRON witnessed | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| - `swaps`: seed swap credits (refusing on overflow) in COBOL | `modular_draw` | num | `seed,members,step` | trebago, sutra | the sortition's modular walk: (seed * 1103515245 + 12345 + step * 7) m | `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6` |
| the test asserts state 3 and that both stay until verified.  | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `bloom`: (critical) bloom timing (degree days to flowering by species table) in FORTRAN as Trebago + APL as KRON witnessed
- - `planting`: planting layout across beds (bloom gaps filled month by month) in APL as KRON
- - `calendar`: bloom calendar grid in APL as KRON
- - `species`: species tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two neighbours report a rare bee and a common one for the same photo: the sighting is BOTH
- never re-derive the tables
