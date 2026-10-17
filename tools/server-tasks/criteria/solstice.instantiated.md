# Criteria (instantiated) — solstice

matrix 43 · wave spec `wave6/043-solstice.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| event record in myra | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- day of week and days in month (Gregorian) in fortran77 + basic + zlir witnessed (the number_classics pattern)
- next date of a repeating event in kron + C++ wasm witnessed
- "two events overlap" in eve + kron witnessed
