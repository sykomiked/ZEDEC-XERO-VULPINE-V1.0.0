# Criteria (instantiated) — typewright

matrix 99 · wave spec `wave8/099-typewright.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| per-key error counts and the weakest keys in kron | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| per-key error counts and the weakest keys in kron | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- words per minute (chars x 12 / seconds, floor) and accuracy x100 in kron + C++ wasm witnessed
- results in myra
