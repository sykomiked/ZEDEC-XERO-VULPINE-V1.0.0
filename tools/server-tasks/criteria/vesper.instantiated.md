# Criteria (instantiated) — vesper

matrix 279 · wave spec `wave23/279-vesper.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `relay_ok` (a warning is relayed at most once per household  | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `relay_ok` (a warning is relayed at most once per household  | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `households` status grid in APL as KRON | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- `spread` (households reached after n hops from the link matrix) in FORTRAN as Trebago + APL as KRON witnessed
- `volunteer_hours` (hours per volunteer per month, refused on overflow) in COBOL as Sutra
- `neighbourhood` tree in MUMPS as MYRA
