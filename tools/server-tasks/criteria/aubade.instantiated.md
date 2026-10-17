# Criteria (instantiated) — aubade

matrix 368 · wave spec `wave31/368-aubade.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `uv_ok`: (critical) UV exposure limits (UV index x minutes | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `uv_ok`: (critical) UV exposure limits (UV index x minutes | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| - `shift_pay`: shift pay (minutes x rate, minor units, refus | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| the test asserts state 3 and that the warning uses 7 while s | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `sun_angle`: (critical) solar elevation by minute (day of year and latitude, integer sine tables) in FORTRAN as Trebago + APL as KRON witnessed
- - `daylight`: daylight grid (days x hours) in APL as KRON
- - `rhythm`: rhythm plan (light before noon, dim after the declared hour) in Ada as Eve
- - `workers`: worker profile tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the forecast says UV 4 and the sensor 7: UV is BOTH
- never re-derive the tables
