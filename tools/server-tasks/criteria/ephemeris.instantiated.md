# Criteria (instantiated) — ephemeris

matrix 299 · wave spec `wave25/299-ephemeris.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `exposure_ok` (exposure and session length inside the declar | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## Clauses the pattern library does not yet cover (author these)

- `orbit` (rise, transit and set in minutes of day) in FORTRAN as Trebago + APL as KRON witnessed
- `equipment` ledger (items in minor units) in COBOL as Sutra
- `sky` coordinate arrays in APL as KRON
- `observations` catalogue in MUMPS as MYRA
