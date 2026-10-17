# Criteria (instantiated) — bindery

matrix 168 · wave spec `wave13/168-bindery.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "may lend" = a copy on the shelf AND the member under their  | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| loan limits in Ada as Eve | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| catalogue grid (title by copy state) in APL as KRON | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- loans ledger (loan number, copy, member, due day
- loans per member refused on overflow) in COBOL as Sutra + APL as KRON witnessed
- demand (loans per title per month, rolling mean floor) exact in FORTRAN as Trebago
- shelf hierarchy in MUMPS as MYRA
