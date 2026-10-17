# Criteria (instantiated) — freshet

matrix 209 · wave spec `wave17/209-freshet.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "within the template's declared limits" (budget, needs only  | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| licence records (project, licence code) as fixed records in  | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- template matrix (template by setting) in APL as KRON
- build timings exact in FORTRAN as Trebago
- project tree in MUMPS as MYRA
