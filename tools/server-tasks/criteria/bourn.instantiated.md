# Criteria (instantiated) — bourn

matrix 249 · wave spec `wave20/249-bourn.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - (critical) strength x100 from gravities x1000 ((OG - FG) x | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| - (critical) duty due (litres x strength x rate, minor units | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| - temperature limits (the yeast's range in tenths of a degre | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| the test asserts state 3 and that the batch is held for a ch | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - batch grid (batches x days x readings) in APL as KRON
- - recipe and lot tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the hydrometer reads 1012 and the sensor 1020, giving BOTH
- never re-derive the tables
