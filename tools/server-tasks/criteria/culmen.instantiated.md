# Criteria (instantiated) — culmen

matrix 305 · wave spec `wave25/305-culmen.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `consolidated` (accounts summed across departments in minor  | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `consolidated` (accounts summed across departments in minor  | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## Clauses the pattern library does not yet cover (author these)

- `risk_ok` (each risk inside the declared appetite) in Ada as Eve + APL as KRON witnessed
- `forecast` (next quarter from the trend) in FORTRAN as Trebago + APL as KRON witnessed
- `kpis` grid in APL as KRON
- `org_chart` tree in MUMPS as MYRA
