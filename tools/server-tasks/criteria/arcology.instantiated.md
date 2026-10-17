# Criteria (instantiated) — arcology

matrix 325 · wave spec `wave27/325-arcology.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `service_level_ok` (a request answered within its declared d | `grade_band` | num | `value,b1,b2,b3,b4` | kron, trebago | the band index 0..4 of the value across the four ascending bounds | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |

## Clauses the pattern library does not yet cover (author these)

- `traffic_utility` (daily demand per district from history) in FORTRAN as Trebago + APL as KRON witnessed
- `revenue` (city revenue per line in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `districts` grid in APL as KRON
- `wards_streets` tree in MUMPS as MYRA
