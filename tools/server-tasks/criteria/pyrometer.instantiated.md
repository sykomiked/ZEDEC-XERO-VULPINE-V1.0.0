# Criteria (instantiated) — pyrometer

matrix 361 · wave spec `wave30/361-pyrometer.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `cutoff`: (critical) temperature cutoffs (any room over it | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `cutoff`: (critical) temperature cutoffs (any room over it | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| - `fuel_bill`: (critical) fuel bills (units x tariff bands,  | `grade_band` | num | `value,b1,b2,b3,b4` | kron, trebago | the band index 0..4 of the value across the four ascending bounds | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| - `heat`: heat transfer model (room heat loss W from area x  | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| - `heat`: heat transfer model (room heat loss W from area x  | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| the test asserts state 3 and that a check is raised, the ala | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `inspect_due`: inspection due (days since each inspection against its interval) in APL as KRON
- - `rooms`: room heat grid in APL as KRON
- - `building`: building tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the smoke alarm sounds and the thermometer shows normal: the room is BOTH
- never re-derive the tables
