# Criteria (instantiated) — bowline

matrix 45 · wave spec `wave6/045-bowline.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| task record in myra | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- next due date (every n days or weeks, skipping chosen weekdays) in basic + kron witnessed
- "overdue" in eve + kron witnessed
- tasks per list in kron
