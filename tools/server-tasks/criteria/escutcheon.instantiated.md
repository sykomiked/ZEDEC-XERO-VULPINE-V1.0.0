# Criteria (instantiated) — escutcheon

matrix 315 · wave spec `wave26/315-escutcheon.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `cover_ok` (a claim inside the policy's cover, limits and da | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## Clauses the pattern library does not yet cover (author these)

- `risk` (expected claims per pool from history, integer) in FORTRAN as Trebago + APL as KRON witnessed
- `premiums_grants` (premiums in, mutual-aid grants out, minor units, refused on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `claims` grid in APL as KRON
- `holders` tree in MUMPS as MYRA
