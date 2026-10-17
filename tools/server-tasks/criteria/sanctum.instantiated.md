# Criteria (instantiated) — sanctum

matrix 295 · wave spec `wave24/295-sanctum.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `withdraw_ok` (amount at most the per-day limit AND the hold | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `withdraw_ok` (amount at most the per-day limit AND the hold | `modular_draw` | num | `seed,members,step` | trebago, sutra | the sortition's modular walk: (seed * 1103515245 + 12345 + step * 7) m | `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6` |
| `valuation` (holdings at the declared prices in minor units) | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## Clauses the pattern library does not yet cover (author these)

- `custody_ledger` (holder, asset, quantity, refused on overflow) in COBOL as Sutra
- `holdings` grid in APL as KRON
- `beneficiaries` tree in MUMPS as MYRA
