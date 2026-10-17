# Criteria (instantiated) — limner

matrix 166 · wave spec `wave13/166-limner.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "may this AI stroke be applied" (accepted by the person AND  | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| asset licences (asset, licence code, attribution) as fixed r | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| canvas limits in Ada as Eve | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## Clauses the pattern library does not yet cover (author these)

- nearest palette colour (smallest squared distance over the palette vectors) in APL as KRON + FORTRAN as Trebago witnessed
- pixel arrays in APL as KRON
- layer tree in MUMPS as MYRA
