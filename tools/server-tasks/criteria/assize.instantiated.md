# Criteria (instantiated) — assize

matrix 178 · wave spec `wave14/178-assize.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "filing accepted" (by a party to the case AND inside the dea | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| days left to respond (deadline day minus today) in FORTRAN a | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| evidence matrix (evidence by claim, four values) in APL as K | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## Clauses the pattern library does not yet cover (author these)

- case docket (case number, filing sequence, never reused) in COBOL as Sutra
- case tree in MUMPS as MYRA
