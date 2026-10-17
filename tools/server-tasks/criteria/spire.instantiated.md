# Criteria (instantiated) — spire

matrix 323 · wave spec `wave27/323-spire.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `extraction_ok` (tonnes at most the permit AND inside the cl | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `ore_body` (tonnage x grade from drill intervals, integer) i | `grade_band` | num | `value,b1,b2,b3,b4` | kron, trebago | the band index 0..4 of the value across the four ascending bounds | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| `royalties` (royalty accounts in minor units) in COBOL as Su | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## Clauses the pattern library does not yet cover (author these)

- `drill` grid in APL as KRON
- `claims` hierarchy in MUMPS as MYRA
