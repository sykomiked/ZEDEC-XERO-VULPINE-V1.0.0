# Criteria (instantiated) — glazier

matrix 193 · wave spec `wave16/193-glazier.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| public accounts per line in minor units in COBOL as Sutra +  | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| budget grid in APL as KRON | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## Clauses the pattern library does not yet cover (author these)

- month-on-month change per line in FORTRAN as Trebago + APL as KRON witnessed
- "may publish" (marked public AND no personal fields AND approved by an owner) in Ada as Eve + APL as KRON witnessed
- agency tree in MUMPS as MYRA
