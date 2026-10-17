# Criteria (instantiated) — orrery

matrix 101 · wave spec `wave9/101-orrery.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| day number from the date and mean longitude per planet (mill | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |

## Clauses the pattern library does not yet cover (author these)

- "above the horizon" in kron
- observations in myra
