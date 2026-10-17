# Criteria (instantiated) — moraine

matrix 210 · wave spec `wave17/210-moraine.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| key-strength score (from lengths and counts only) in FORTRAN | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| key-strength score (from lengths and counts only) in FORTRAN | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- "release now" (Heirloom's release due AND access window not over AND requester allowed) in Ada as Eve + APL as KRON witnessed
- custody register (release number, guardian, time, never reused) in COBOL as Sutra
- access grid in APL as KRON
- guardian hierarchy in MUMPS as MYRA
