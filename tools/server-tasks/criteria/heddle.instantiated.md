# Criteria (instantiated) — heddle

matrix 162 · wave spec `wave13/162-heddle.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| drawdown (threading x tie-up x treadling, as an integer matr | `modular_draw` | num | `seed,members,step` | trebago, sutra | the sortition's modular walk: (seed * 1103515245 + 12345 + step * 7) m | `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6` |
| drawdown (threading x tie-up x treadling, as an integer matr | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| yarn length per colour and cost in minor units in COBOL as S | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| "fits the machine" (shafts, width, colours at most the loom' | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## Clauses the pattern library does not yet cover (author these)

- ends (ends per 10 cm x width) and picks exact in FORTRAN as Trebago
- pattern tree in MUMPS as MYRA
