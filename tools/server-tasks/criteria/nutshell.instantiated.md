# Criteria (instantiated) — nutshell

matrix 106 · wave spec `wave9/106-nutshell.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| due counts in kron | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## Clauses the pattern library does not yet cover (author these)

- next box and due day (Leitner: up on right, back to 1 on wrong) in basic + kron witnessed
- decks and reviews in myra
