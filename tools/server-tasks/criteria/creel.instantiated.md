# Criteria (instantiated) — creel

matrix 187 · wave spec `wave15/187-creel.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| quota left (quota minus landed, refused below zero) in COBOL | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| catch sales in minor units in COBOL as Sutra + FORTRAN as Tr | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## Clauses the pattern library does not yet cover (author these)

- "over quota" in Ada as Eve + APL as KRON witnessed
- stock trend (landings per season) exact in FORTRAN as Trebago
- catch grid in APL as KRON
- vessel tree in MUMPS as MYRA
