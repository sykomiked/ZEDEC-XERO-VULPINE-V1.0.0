# Criteria (instantiated) — wordhoard

matrix 46 · wave spec `wave6/046-wordhoard.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| headword index and prefix lookup in myra | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| synonym order by shared senses in kron | `share_x100` | num | `part,whole` | kron, trebago | the part's share of the whole in hundredths of a percent, floor: part  | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |

## Clauses the pattern library does not yet cover (author these)

- "did you mean" edit distance over code points in C++ wasm
