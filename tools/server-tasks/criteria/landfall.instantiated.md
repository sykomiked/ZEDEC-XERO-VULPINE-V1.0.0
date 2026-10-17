# Criteria (instantiated) — landfall

matrix 356 · wave spec `wave30/356-landfall.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `turnaround`: (critical) "turn around now" (past the turna | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `permits`: permit fees (minor units, refusing on overflow) | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| the test asserts state 3 and that an area covering both is s | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `overdue`: (critical) "raise overdue" (no check-in by the declared return time plus grace) in Ada as Eve + APL as KRON witnessed
- the alert goes only to contacts the person named
- - `energy`: route energy model (ascent and distance to kcal and hours, integer tables) in FORTRAN as Trebago
- - `waypoints`: waypoint arrays in APL as KRON
- - `roster`: team roster tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the satellite fix and dead reckoning differ by 900 m: the position is BOTH
- never re-derive the tables
