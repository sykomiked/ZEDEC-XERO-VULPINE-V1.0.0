# Criteria (instantiated) — fieldguide

matrix 102 · wave spec `wave9/102-fieldguide.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| identification key, one question at a time, in basic | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| article index in myra | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- "show a caution" (any matched species marked toxic or dangerous) in kron + myra witnessed
- match ranking in kron
- Never says a plant or fungus is safe to eat
