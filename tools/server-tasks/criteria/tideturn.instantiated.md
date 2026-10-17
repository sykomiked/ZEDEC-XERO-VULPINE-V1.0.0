# Criteria (instantiated) — tideturn

matrix 6 · wave spec `wave2/006-tideturn.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| update record in myra | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- version ordering (major.minor.patch compare) in sutra + kron witnessed
- "both signatures present and valid" as a truth in eve + kron (inputs are the two verify results as 0/1)
- Never install anything in tests
- use the daemon's check only
