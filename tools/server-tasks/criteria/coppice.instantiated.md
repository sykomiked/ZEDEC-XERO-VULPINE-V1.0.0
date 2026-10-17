# Criteria (instantiated) — coppice

matrix 192 · wave spec `wave15/192-coppice.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| carbon held and timber volume (volume x density x factor, ki | `within_range` | truth | `value,min,max` | eve, kron | true when min <= value <= max | `check within_range value=0,50,100,101 min=0,0,0,0 max=100,100,100,100 = true,true,true,false` |
| "felling allowed" (at most the stand's annual growth AND not | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## Clauses the pattern library does not yet cover (author these)

- diameter growth per year in FORTRAN as Trebago + APL as KRON witnessed
- stand grid in APL as KRON
- tree inventory in MUMPS as MYRA
