# Criteria (instantiated) — bearing

matrix 401 · wave spec `wave33/401-bearing.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `connect_ok`: (critical) connection limits (each transfer  | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `timing`: (critical) route timing (arrival minutes as a ru | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| - `options`: itinerary search over alternatives (every combi | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| the test asserts state 3 and that both plans are shown. Join | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `timetable`: timetable grid in APL as KRON
- - `tickets`: ticket payments (minor units, refusing on overflow) in COBOL as Sutra
- - `itinerary`: itinerary tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the timetable says 14:05 and live data says 14:40: the departure is BOTH
- never re-derive the tables
