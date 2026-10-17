# Criteria (instantiated) — loom

matrix 265 · wave spec `wave22/265-loom.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - (critical) "this wire is valid" (output kind matches input | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - (critical) budget total over the wires within the app's bu | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - composition log (wires added and removed, counts refusing  | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - composition log (wires added and removed, counts refusing  | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |
| the test asserts state 3 and that the board shows both, no s | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - board grid (blocks x places) in APL as KRON
- - block tree (block, version, CID, wires) in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two blocks give different answers on the same wire, giving BOTH
- never re-derive the tables
