# Criteria (instantiated) — pinion

matrix 317 · wave spec `wave26/317-pinion.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `wear` (remaining hours from load history) in FORTRAN as Tre | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `maintenance_costs` (parts and labour in minor units) in COB | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## Clauses the pattern library does not yet cover (author these)

- `interlock_ok` (a machine runs only with every interlock closed and no safety stop latched) in Ada as Eve + APL as KRON witnessed
- `sensors` arrays in APL as KRON
- `machines` hierarchy in MUMPS as MYRA
