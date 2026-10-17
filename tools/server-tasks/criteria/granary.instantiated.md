# Criteria (instantiated) — granary

matrix 64 · wave spec `wave7/064-granary.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| memory need (parameters x bits / 8) exact in trebago | `flags_none` | truth | `bits,forbidden` | eve | true when (bits & forbidden) == 0 (no refused bit set) | `check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false` |
| model record in myra | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- licence allows this use (licence id in the allowed list, personal or commercial flag) in eve + kron witnessed
- "fits on disk" in sutra + kron witnessed
- A model whose licence does not allow the use is never loaded
- the owner decides
