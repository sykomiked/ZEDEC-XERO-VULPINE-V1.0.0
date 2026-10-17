# Criteria (instantiated) — cleave

matrix 310 · wave spec `wave26/310-cleave.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `blast_ok` (a test touches only targets in its declared blas | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `test_budget` (fuel per test run, refused on overflow) in CO | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## Clauses the pattern library does not yet cover (author these)

- `load_gen` (requests per second ramp over time) in FORTRAN as Trebago + APL as KRON witnessed
- `failures` matrix in APL as KRON
- `components` tree in MUMPS as MYRA
