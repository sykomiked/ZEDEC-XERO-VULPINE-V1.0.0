# Criteria (instantiated) — astrolabe

matrix 160 · wave spec `wave13/160-astrolabe.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "session go" (cloud at most the limit AND the object up in t | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| "session go" (cloud at most the limit AND the object up in t | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| exposure limits (milliseconds per frame, frames per session) | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## Clauses the pattern library does not yet cover (author these)

- next time an object is above the declared altitude inside the night window (minutes of day) in FORTRAN as Trebago + APL as KRON witnessed
- star catalogue arrays (objects up tonight, by magnitude and altitude) in APL as KRON
- object hierarchy and sightings in MUMPS as MYRA
