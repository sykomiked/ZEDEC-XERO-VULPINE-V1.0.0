# Criteria (instantiated) — fiddlehead

matrix 57 · wave spec `wave7/057-fiddlehead.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| tempo and pattern limits in eve | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| loop record in myra | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- beat grid (samples per step at a tempo, exact) in trebago + kron witnessed
- pitch (equal temperament from a 12-entry milli-hertz table, octaves by doubling) in fortran77 + zlir witnessed
