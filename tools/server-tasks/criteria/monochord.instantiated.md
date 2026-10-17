# Criteria (instantiated) — monochord

matrix 343 · wave spec `wave28/343-monochord.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `laser_ok` (laser class and exposure time inside the bench's | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `line_fit` (peak centre from the integer-weighted centroid o | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## Clauses the pattern library does not yet cover (author these)

- `grant_spend` (in minor units) in COBOL as Sutra
- `spectra` arrays in APL as KRON
- `samples` hierarchy in MUMPS as MYRA
