# Criteria (instantiated) — foghorn

matrix 400 · wave spec `wave33/400-foghorn.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `timing_ok`: (critical) signal timing limits (each light's | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| the test asserts state 3 and that it is broadcast as BOTH wi | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `visibility`: (critical) visibility range (nominal range by intensity and the met visibility, integer table) in FORTRAN as Trebago + APL as KRON witnessed
- - `broadcast`: hazard broadcast routing (area to channel) in Ada as Eve
- - `beacons`: beacon grid in APL as KRON
- - `dues`: light dues (minor units, refusing on overflow) in COBOL as Sutra
- - `harbour`: harbour tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two reports of a wreck give different positions: the hazard is BOTH
- never re-derive the tables
