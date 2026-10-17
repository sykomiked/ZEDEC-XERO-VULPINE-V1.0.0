# Criteria (instantiated) — distaff

matrix 389 · wave spec `wave32/389-distaff.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `bandwidth_ok`: (critical) bandwidth limits (bytes served  | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `bandwidth_ok`: (critical) bandwidth limits (bytes served  | `grade_band` | num | `value,b1,b2,b3,b4` | kron, trebago | the band index 0..4 of the value across the four ascending bounds | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| the test asserts state 3 and that readers see the change, no | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `link_ok`: (critical) "link resolves" (the CID is well formed AND the target's signature verifies) in Ada as Eve + APL as KRON witnessed
- - `rank`: link ranking (in-links weighted by depth) exact in FORTRAN as Trebago
- - `links`: link matrix in APL as KRON
- - `credits`: hosting credits (refusing on overflow) in COBOL as Sutra
- - `site`: site tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two versions of a linked page exist under one name: the link is BOTH
- never re-derive the tables
