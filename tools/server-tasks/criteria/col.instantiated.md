# Criteria (instantiated) — col

matrix 269 · wave spec `wave22/269-col.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - (critical) "let this connection through" (rate under the l | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - (critical) "let this connection through" (rate under the l | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| the test asserts state 3 and that the gate asks the person,  | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - (critical) bytes per window per app (traffic model) in FORTRAN as Trebago + APL as KRON witnessed
- - route table in APL as KRON
- - access ledger (bytes and connections, refusing on overflow) in COBOL as Sutra
- - identity tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two identity claims for one name conflict, giving BOTH
- never re-derive the tables
