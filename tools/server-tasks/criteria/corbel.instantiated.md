# Criteria (instantiated) — corbel

matrix 338 · wave spec `wave28/338-corbel.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `capacity_ok` (visitors in a room at most its declared capac | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `capacity_ok` (visitors in a room at most its declared capac | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `wear` (footfall x the floor's declared wear rate) in FORTRA | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |

## Clauses the pattern library does not yet cover (author these)

- `ticket_revenue` (in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `site` grid in APL as KRON
- `artefacts` hierarchy in MUMPS as MYRA
