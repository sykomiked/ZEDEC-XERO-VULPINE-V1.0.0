# Criteria (instantiated) — annulus

matrix 285 · wave spec `wave24/285-annulus.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `dose_ok` (each dose and the day's total at most the prescri | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `dose_ok` (each dose and the day's total at most the prescri | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `kinetics` (amount remaining after t hours by the declared h | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `kinetics` (amount remaining after t hours by the declared h | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## Clauses the pattern library does not yet cover (author these)

- `dispensing` (dispense number, item, quantity, refused on overflow) in COBOL as Sutra
- `interactions` grid (medicine by medicine) in APL as KRON
- `medications` tree in MUMPS as MYRA
