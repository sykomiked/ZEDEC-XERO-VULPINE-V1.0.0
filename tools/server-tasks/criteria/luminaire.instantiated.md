# Criteria (instantiated) — luminaire

matrix 308 · wave spec `wave26/308-luminaire.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `circuit_ok` (lights on a circuit at most its declared load) | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `daylight` (daylight minutes and level by hour from the date | `grade_band` | num | `value,b1,b2,b3,b4` | kron, trebago | the band index 0..4 of the value across the four ascending bounds | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |

## Clauses the pattern library does not yet cover (author these)

- `energy_bills` (Wh x tariff in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `rooms` light grid in APL as KRON
- `zones` tree in MUMPS as MYRA
