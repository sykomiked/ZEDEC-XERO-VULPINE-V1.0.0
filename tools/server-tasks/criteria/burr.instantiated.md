# Criteria (instantiated) — burr

matrix 118 · wave spec `wave10/118-burr.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| limits (at most 256 notes, text at most 49152 bytes, the rea | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| limits (at most 256 notes, text at most 49152 bytes, the rea | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## Clauses the pattern library does not yet cover (author these)

- placement of a new note (cascade by a step, wrap to the top when off screen) in kron + trebago witnessed
