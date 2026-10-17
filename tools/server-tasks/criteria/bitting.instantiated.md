# Criteria (instantiated) — bitting

matrix 307 · wave spec `wave25/307-bitting.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `window_ok` (a key opens only inside its time window) in Ada | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `deposits` (key deposits in minor units) in COBOL as Sutra + | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `doors` permission grid in APL as KRON | `flags_none` | truth | `bits,forbidden` | eve | true when (bits & forbidden) == 0 (no refused bit set) | `check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- `access_pattern` (uses per hour against the holder's usual pattern, integer score) in FORTRAN as Trebago + APL as KRON witnessed
- `holders` tree in MUMPS as MYRA
