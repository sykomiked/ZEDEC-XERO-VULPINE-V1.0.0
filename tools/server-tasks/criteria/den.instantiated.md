# Criteria (instantiated) — den

matrix 97 · wave spec `wave4/097-den.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "may this game start now" (minutes left, age rating at or be | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |

## Clauses the pattern library does not yet cover (author these)

- play time per game and sorting in kron
- the built-in classic type-in games' turn logic in basic
- library in myra
