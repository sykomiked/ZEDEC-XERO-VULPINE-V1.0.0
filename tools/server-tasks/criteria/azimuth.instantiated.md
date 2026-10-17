# Criteria (instantiated) — azimuth

matrix 306 · wave spec `wave25/306-azimuth.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `accessible_ok` (a route has no step, gradient over the decl | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## Clauses the pattern library does not yet cover (author these)

- `position` (position from beacon ranges, integer least squares) in FORTRAN as Trebago + APL as KRON witnessed
- `venue_licence` (venue licences in minor units) in COBOL as Sutra
- `floors` grid in APL as KRON
- `buildings` tree in MUMPS as MYRA
