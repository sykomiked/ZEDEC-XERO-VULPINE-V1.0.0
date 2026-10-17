# Criteria (instantiated) — quay

matrix 316 · wave spec `wave26/316-quay.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `berth_ok` (vessel draught at most the berth depth at the ti | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `harbour_dues` (length x nights x rate in minor units) in CO | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |

## Clauses the pattern library does not yet cover (author these)

- `berth_schedule` (arrivals to berths with no overlap, earliest first) in FORTRAN as Trebago + APL as KRON witnessed
- `berths` grid in APL as KRON
- `vessels_cargo` tree in MUMPS as MYRA
