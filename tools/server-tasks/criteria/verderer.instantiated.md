# Criteria (instantiated) — verderer

matrix 176 · wave spec `wave14/176-verderer.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| population estimate (marked x caught / recaptured, floor, re | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| "share this place" (a protected species' place is coarsened  | `share_x100` | num | `part,whole` | kron, trebago | the part's share of the whole in hundredths of a percent, floor: part  | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| protected-area limits in Ada as Eve | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## Clauses the pattern library does not yet cover (author these)

- sighting grid (species by grid cell) in APL as KRON
- species tree in MUMPS as MYRA
