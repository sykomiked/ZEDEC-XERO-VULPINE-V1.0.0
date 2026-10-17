# Criteria (instantiated) — chandler

matrix 161 · wave spec `wave13/161-chandler.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| provisions (days of supply = stock / (crew x daily ration),  | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| "within load limits" (cargo, stores and crew at most the ves | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| "within load limits" (cargo, stores and crew at most the ves | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## Clauses the pattern library does not yet cover (author these)

- tide height at a time by the rule of twelfths from high and low water (millimetres) in FORTRAN as Trebago + APL as KRON witnessed
- tide tables in APL as KRON
- crew and vessel tree in MUMPS as MYRA
