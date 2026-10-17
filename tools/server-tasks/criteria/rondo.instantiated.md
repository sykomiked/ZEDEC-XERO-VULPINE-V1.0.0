# Criteria (instantiated) — rondo

matrix 370 · wave spec `wave31/370-rondo.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `share_ok`: (critical) privacy limits (nothing leaves the  | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `share_ok`: (critical) privacy limits (nothing leaves the  | `share_x100` | num | `part,whole` | kron, trebago | the part's share of the whole in hundredths of a percent, floor: part  | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| - `notes`: shared care notes (bounded text, kept in C) count | `share_x100` | num | `part,whole` | kron, trebago | the part's share of the whole in hundredths of a percent, floor: part  | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| - `notes`: shared care notes (bounded text, kept in C) count | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| the test asserts state 3 and that the person's log stands an | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `predict`: (critical) cycle prediction (mean and spread of the last n cycle lengths in days, the next window) in FORTRAN as Trebago + APL as KRON witnessed
- - `days`: day grid (days x symptoms) in APL as KRON
- - `billing`: clinic billing (minor units, refusing on overflow) in COBOL as Sutra
- - `health`: health tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the predicted start is day 28 and the logged start day 31: the cycle is BOTH
- never re-derive the tables
