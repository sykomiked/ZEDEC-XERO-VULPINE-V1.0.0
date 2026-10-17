# Criteria (instantiated) — tracery

matrix 275 · wave spec `wave23/275-tracery.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `cull_ok` (a cull or take at most the quota AND the populati | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `population` (herd trend from survey counts) in FORTRAN as T | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `permit_fee` (permits in minor units) in COBOL as Sutra + FO | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## Clauses the pattern library does not yet cover (author these)

- `sightings` grid in APL as KRON
- `species` tree in MUMPS as MYRA
