# Criteria (instantiated) — barbican

matrix 272 · wave spec `wave23/272-barbican.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `breach_cost` (cost of a breach as declared items in minor u | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `identity` tree (person, devices, keys) in MUMPS as MYRA | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- `lockout_ok` (lock a login out after the declared failed attempts inside the window, unlock only by the owner) in Ada as Eve + APL as KRON witnessed
- `anomaly` (failed attempts per device per hour against the device's 14-day mean, integer score) in FORTRAN as Trebago + APL as KRON witnessed
- `threat_grid` (device by attempt kind) in APL as KRON
- the plain-words message is chosen in C from the decision codes
