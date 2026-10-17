# Criteria (instantiated) — sunfleck

matrix 330 · wave spec `wave27/330-sunfleck.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `issue_ok` (credits issued at most the verified carbon held, | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `biomass` (carbon per stand from counts and girth, integer)  | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `credit_sales` (in minor units) in COBOL as Sutra + FORTRAN  | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## Clauses the pattern library does not yet cover (author these)

- `plots` grid in APL as KRON
- `stands` hierarchy in MUMPS as MYRA
