# Criteria (instantiated) — caravan

matrix 90 · wave spec `wave1/090-caravan.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "within carrier limits" (mass, length, girth) in eve + treba | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| shipment records in myra | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- charged weight = max(mass, ceil(L x W x H / divisor)) in trebago + sutra witnessed
- route choice from `use pce_strat 1` (C, advisory)
