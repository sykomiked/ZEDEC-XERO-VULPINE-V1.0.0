# Criteria (instantiated) — atrium

matrix 332 · wave spec `wave28/332-atrium.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `capacity_ok` (guests per room and per venue at most the dec | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `capacity_ok` (guests per room and per venue at most the dec | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `no_double_booking` (a room has at most one booking per nigh | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## Clauses the pattern library does not yet cover (author these)

- `occupancy` (next month's occupancy from bookings and history) in FORTRAN as Trebago + APL as KRON witnessed
- `folios` (guest bills in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `rooms` grid in APL as KRON
- `guests` history in MUMPS as MYRA
