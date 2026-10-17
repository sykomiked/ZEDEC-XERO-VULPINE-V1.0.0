# Criteria (instantiated) — aureole

matrix 277 · wave spec `wave23/277-aureole.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `motion_passes` (quorum present AND votes for over the decla | `share_x100` | num | `part,whole` | kron, trebago | the part's share of the whole in hundredths of a percent, floor: part  | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `vote_tally` (counts per option over the ballot vector) in A | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `budget_lines` (municipal budget per line in minor units, re | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## Clauses the pattern library does not yet cover (author these)

- `revenue` (next year's revenue from the trend) in FORTRAN as Trebago
- `bylaws` hierarchy in MUMPS as MYRA
