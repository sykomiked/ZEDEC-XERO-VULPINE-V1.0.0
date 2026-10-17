# Criteria (instantiated) — damper

matrix 174 · wave spec `wave14/174-damper.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "raise the alarm" (any sensor over its temperature limit OR  | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| temperature rise per minute over the last readings (tenths o | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| temperature limits in Ada as Eve | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## Clauses the pattern library does not yet cover (author these)

- sensor grid (room by sensor) in APL as KRON
- building tree (floors, rooms, exits) in MUMPS as MYRA
