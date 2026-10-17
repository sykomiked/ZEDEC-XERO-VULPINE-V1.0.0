# Criteria (instantiated) — armillary

matrix 349 · wave spec `wave29/349-armillary.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `drift_ok`: (critical) torque and drift limits (torque at  | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `drift_ok`: (critical) torque and drift limits (torque at  | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - `calibrate`: (critical) calibration curve (two-point and m | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| - `calibrate`: (critical) calibration curve (two-point and m | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| - `contracts`: service contracts (visits used, cost in minor | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| the test asserts state 3 and that a recalibration is schedul | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| the test asserts state 3 and that a recalibration is schedul | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `sensors`: sensor matrices (sensors x axes, alignment offsets) in APL as KRON
- - `assembly`: assembly tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two sensors on one axis read 12.004 mm and 12.019 mm with a 5 um tolerance: the axis is BOTH
- never re-derive the tables
