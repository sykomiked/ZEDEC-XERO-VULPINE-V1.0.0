# Criteria (instantiated) — verger

matrix 206 · wave spec `wave17/206-verger.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "may share this member's details" (the member's recorded con | `share_x100` | num | `part,whole` | kron, trebago | the part's share of the whole in hundredths of a percent, floor: part  | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| "may share this member's details" (the member's recorded con | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- giving per fund in minor units in COBOL as Sutra + FORTRAN as Trebago witnessed
- attendance trend in FORTRAN as Trebago + APL as KRON witnessed
- rota grid in APL as KRON
- member tree in MUMPS as MYRA
