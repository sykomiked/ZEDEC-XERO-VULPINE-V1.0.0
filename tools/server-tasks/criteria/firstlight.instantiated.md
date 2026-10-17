# Criteria (instantiated) — firstlight

matrix 304 · wave spec `wave25/304-firstlight.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `screen_ok` (minutes left today AND inside the parents' hour | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |

## Clauses the pattern library does not yet cover (author these)

- `pace` (lessons a week at the child's recent pace) in FORTRAN as Trebago + APL as KRON witnessed
- `pocket_money` (a play ledger in minor units, never real money) in COBOL as Sutra
- `puzzles` grids in APL as KRON
- first coding steps in BASIC (twin Hatchling)
- `child_profile` tree in MUMPS as MYRA
