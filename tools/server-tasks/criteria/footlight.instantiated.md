# Criteria (instantiated) — footlight

matrix 41 · wave spec `wave6/041-footlight.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| slide record in myra | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- slide scale to fit the window (16:9 into any size, letterbox) in kron + C++ wasm witnessed
- rehearsal timing (seconds per slide, total against talk length) exact in trebago
- run order (advance, hold, loop) in basic
