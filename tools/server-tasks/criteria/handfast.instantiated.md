# Criteria (instantiated) — handfast

matrix 164 · wave spec `wave13/164-handfast.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| payment schedule (instalment n = floor(total x n / N) minus  | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| clause limits (at most 16 parties, clause text at most 8192  | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| clause limits (at most 16 parties, clause text at most 8192  | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| party matrix (party by clause, four values) in APL as KRON | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## Clauses the pattern library does not yet cover (author these)

- "ready to sign" = every clause agreed by every party (the party matrix all TRUE) in Ada as Eve + APL as KRON witnessed
- obligations per party per due date in minor units in COBOL as Sutra + FORTRAN as Trebago witnessed
- clause tree in MUMPS as MYRA
