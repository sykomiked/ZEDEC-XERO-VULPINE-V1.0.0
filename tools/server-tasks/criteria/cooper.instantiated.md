# Criteria (instantiated) — cooper

matrix 208 · wave spec `wave17/208-cooper.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "next stage allowed" (an item in one state at a time: a scra | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |
| "within waste limits" in Ada as Eve | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| product tree in MUMPS as MYRA | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## Clauses the pattern library does not yet cover (author these)

- material ledger (grams per material per stage) in COBOL as Sutra + FORTRAN as Trebago witnessed
- uses per item over its expected life in FORTRAN as Trebago
- flow grid (item by stage) in APL as KRON
