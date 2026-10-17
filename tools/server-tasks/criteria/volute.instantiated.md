# Criteria (instantiated) — volute

matrix 280 · wave spec `wave23/280-volute.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `stocking_ok` (kilograms of stock per cubic metre at most th | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `harvest_sales` (kg x price in minor units) in COBOL as Sutr | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `stock_health` records in MUMPS as MYRA | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- `oxygen` (dissolved oxygen trend and growth per day, integer model) in FORTRAN as Trebago + APL as KRON witnessed
- `pens` sensor arrays in APL as KRON
