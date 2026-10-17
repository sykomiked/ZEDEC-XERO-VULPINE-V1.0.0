# Criteria (instantiated) — paisley

matrix 373 · wave spec `wave31/373-paisley.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `print_ok`: (critical) printer limits (size, colours and r | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| the test asserts state 3 and that both are kept as variants. | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `symmetry`: (critical) radial symmetry (rotate each motif point by 360/n using an integer sine table, mirrored when set) in FORTRAN as Trebago + APL as KRON witnessed
- - `toolpath`: cut and stitch path order over the motif (shortest pen-up travel) in APL as KRON
- - `patterns`: pattern arrays in APL as KRON
- - `orders`: print orders (minor units, refusing on overflow) in COBOL as Sutra
- - `versions`: design version tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two collaborators edit the same motif differently: it is BOTH
- never re-derive the tables
