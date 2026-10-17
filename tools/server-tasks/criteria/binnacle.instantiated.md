# Criteria (instantiated) — binnacle

matrix 147 · wave spec `wave12/147-binnacle.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| a fix from two lines of position (2x2 integer system by Cram | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| "safe heading" (0..35999 centidegrees, outside every marked  | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## Clauses the pattern library does not yet cover (author these)

- bearing and distance to every waypoint (fixed point, over the waypoint vectors) in APL as KRON
- distance run per leg in metres, refused on overflow, in COBOL as Sutra + FORTRAN as Trebago witnessed
- waypoint tree in MUMPS as MYRA
