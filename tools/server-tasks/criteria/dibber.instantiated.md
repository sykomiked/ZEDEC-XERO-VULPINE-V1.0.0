# Criteria (instantiated) — dibber

matrix 149 · wave spec `wave12/149-dibber.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "may water now" (within the week's water budget AND no frost | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| water limits in Ada as Eve | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| seed and supply costs in minor units in COBOL as Sutra + FOR | `modular_draw` | num | `seed,members,step` | trebago, sutra | the sortition's modular walk: (seed * 1103515245 + 12345 + step * 7) m | `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6` |
| seed and supply costs in minor units in COBOL as Sutra + FOR | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| days to harvest from growing degree days (sum of max(0, mean | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |

## Clauses the pattern library does not yet cover (author these)

- water per bed per week (bed area in square decimetres x depth in mm = litres) exact in FORTRAN as Trebago + APL as KRON witnessed
- bed grid (crop family per bed per year, no repeat on the same bed) in APL as KRON
- plot hierarchy in MUMPS as MYRA
