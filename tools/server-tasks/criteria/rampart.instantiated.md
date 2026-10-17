# Criteria (instantiated) — rampart

matrix 289 · wave spec `wave24/289-rampart.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `incident_costs` (hours and items in minor units) in COBOL a | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## Clauses the pattern library does not yet cover (author these)

- `playbook_ok` (a response step allowed by the incident's playbook AND approved by the responder on duty) in Ada as Eve + APL as KRON witnessed
- `risk` (likelihood x impact per asset, integer) in FORTRAN as Trebago + APL as KRON witnessed
- `correlation` grid (alert by asset by hour) in APL as KRON
- `assets` hierarchy in MUMPS as MYRA
