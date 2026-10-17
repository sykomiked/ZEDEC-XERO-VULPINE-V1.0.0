# Criteria (instantiated) — ravelin

matrix 333 · wave spec `wave28/333-ravelin.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `failure_model` (hours of supply left on backup from load an | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `incident_costs` (in minor units) in COBOL as Sutra | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## Clauses the pattern library does not yet cover (author these)

- `interlock_ok` (a door, breaker or valve action allowed only with the site's interlocks satisfied and two people approving) in Ada as Eve + APL as KRON witnessed
- `zones` sensor grid in APL as KRON
- `site_assets` tree in MUMPS as MYRA
