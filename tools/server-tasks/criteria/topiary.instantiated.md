# Criteria (instantiated) — topiary

matrix 204 · wave spec `wave16/204-topiary.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| streak length (days done in a row, a BOTH day counts as not  | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| "may nudge now" (at most the daily count, not in quiet hours | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## Clauses the pattern library does not yet cover (author these)

- weekly trend exact in FORTRAN as Trebago
- day grid in APL as KRON
- goal tree in MUMPS as MYRA
