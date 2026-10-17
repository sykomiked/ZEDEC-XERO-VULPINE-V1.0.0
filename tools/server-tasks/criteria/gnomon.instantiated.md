# Criteria (instantiated) — gnomon

matrix 107 · wave spec `wave9/107-gnomon.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| fractions reduced with `use number_classics 1` | `share_x100` | num | `part,whole` | kron, trebago | the part's share of the whole in hundredths of a percent, floor: part  | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |

## Clauses the pattern library does not yet cover (author these)

- twice a polygon's area (shoelace, exact) and squared side lengths in kron + C++ wasm witnessed
- saved figures in myra. Turtle paths stay in C (Logo has no runtime)
- say so in the manifest
