# Criteria (instantiated) — terroir

matrix 231 · wave spec `wave19/231-terroir.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - (critical) "may spray this block" (days since last spray a | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - (critical) excise due (litres x strength x rate, minor uni | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| the test asserts state 3 and that both are shown and no harv | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - ripening model (growing degree days from tenths of a degree, sugar trend) exact in FORTRAN as Trebago
- - block grid (blocks x rows x readings) in APL as KRON
- - vine and lot tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the lab reads sugar 240 (tenths of a degree Brix) and the field refractometer 215, giving BOTH
- never re-derive the tables
