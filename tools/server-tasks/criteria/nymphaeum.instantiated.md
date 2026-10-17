# Criteria (instantiated) — nymphaeum

matrix 334 · wave spec `wave28/334-nymphaeum.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `safe_ok` (every measured contaminant at most its declared s | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `safe_ok` (every measured contaminant at most its declared s | `grade_band` | num | `value,b1,b2,b3,b4` | kron, trebago | the band index 0..4 of the value across the four ascending bounds | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| `testing_costs` (in minor units) in COBOL as Sutra | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## Clauses the pattern library does not yet cover (author these)

- `contaminant` (trend per contaminant, integer) in FORTRAN as Trebago + APL as KRON witnessed
- `samples` grid in APL as KRON
- `taps_sources` tree in MUMPS as MYRA
