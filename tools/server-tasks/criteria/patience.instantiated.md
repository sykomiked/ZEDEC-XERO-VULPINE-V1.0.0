# Criteria (instantiated) — patience

matrix 105 · wave spec `wave9/105-patience.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "legal move" (rank one lower, alternating colour, empty colu | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |
| deal from a seed and score in kron | `modular_draw` | num | `seed,members,step` | trebago, sutra | the sortition's modular walk: (seed * 1103515245 + 12345 + step * 7) m | `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6` |

## Clauses the pattern library does not yet cover (author these)

- games in myra
