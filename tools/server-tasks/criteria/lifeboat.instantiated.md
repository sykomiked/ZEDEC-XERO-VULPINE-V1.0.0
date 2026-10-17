# Criteria (instantiated) — lifeboat

matrix 11 · wave spec `wave4/011-lifeboat.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "safe to start normally" from the check results (journal int | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| recovery record in myra | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- next repair step from those results in basic + kron witnessed
- "restore fits" (bytes needed vs free) in sutra + kron witnessed
- Nothing is restored, switched or reinstalled without the owner's approval
- restore always makes a new item. Tests use a scratch Hoard and books, never the real ones
