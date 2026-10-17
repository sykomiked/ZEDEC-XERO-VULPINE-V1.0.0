# Criteria (instantiated) — fanfare

matrix 364 · wave spec `wave30/364-fanfare.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `gaming_ok`: (critical) anti-gaming limits (self-award ref | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `gaming_ok`: (critical) anti-gaming limits (self-award ref | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - `report`: public contribution report (counts per member pe | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| the test (scratch books) asserts state 3 and that its credit | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `reputation`: (critical) reputation score (contributions weighted by kind, decaying by a declared half-life table) in FORTRAN as Trebago + APL as KRON witnessed
- - `credits`: credit ledger (minor units, refusing on overflow) in COBOL as Sutra
- - `members`: member tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): a contribution claimed by two members: it is BOTH
- never re-derive the tables
