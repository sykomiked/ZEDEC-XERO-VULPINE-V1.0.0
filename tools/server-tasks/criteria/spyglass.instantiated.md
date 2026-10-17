# Criteria (instantiated) — spyglass

matrix 117 · wave spec `wave10/117-spyglass.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| focus-follow step (at most N pixels a frame toward the targe | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## Clauses the pattern library does not yet cover (author these)

- source rectangle for a pointer and zoom (q8, 1x..16x, kept inside the screen) in kron + trebago witnessed
