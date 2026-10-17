# Criteria (instantiated) — elevation

matrix 301 · wave spec `wave25/301-elevation.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `code_ok` (each declared code clause met: every clause's res | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `code_ok` (each declared code clause met: every clause's res | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `cost_estimate` (in minor units) in COBOL as Sutra + FORTRAN | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `drawings` grid in APL as KRON | `modular_draw` | num | `seed,members,step` | trebago, sutra | the sortition's modular walk: (seed * 1103515245 + 12345 + step * 7) m | `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6` |

## Clauses the pattern library does not yet cover (author these)

- `structure` (member forces and deflection, integer) in FORTRAN as Trebago + APL as KRON witnessed
- `elements` tree in MUMPS as MYRA
