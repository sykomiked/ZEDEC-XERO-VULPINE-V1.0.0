# Criteria (instantiated) — assay

matrix 72 · wave spec `wave2/072-assay.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| pass share x100 per agent in kron + trebago witnessed | `share_x100` | num | `part,whole` | kron, trebago | the part's share of the whole in hundredths of a percent, floor: part  | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| a run's four counts (TRUE FALSE BOTH NEITHER) decided with ` | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| ranking over the score vector in kron. Run log stays in the  | `clamp` | num | `value,min,max` | kron, trebago | value clamped into [min,max] | `check clamp value=-5,0,50,150 min=0,0,0,0 max=100,100,100,100 = 0,0,50,100` |

## Clauses the pattern library does not yet cover (author these)

- "meets the bar" truth (passes, refusals, minimum runs) in eve + kron witnessed
