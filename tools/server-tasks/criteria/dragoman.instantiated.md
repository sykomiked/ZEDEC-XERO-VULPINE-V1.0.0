# Criteria (instantiated) — dragoman

matrix 244 · wave spec `wave20/244-dragoman.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - (critical) "this language runs" (runtime column not `-` AN | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - (critical) runtime limits (fuel, output at most 49152 byte | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - (critical) runtime limits (fuel, output at most 49152 byte | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - each pillar hosts its legacy language's conformance sample | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - each pillar hosts its legacy language's conformance sample | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| the test asserts state 3 and that both results are kept for  | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - language to AI-twin pairing (language, twin, runtime) in MUMPS as MYRA
- - C++ wasm is not used: no runtime row for Dragoman
- Paraconsistent (BOTH test, in test_zxapp.c): one program run in Sutra and in Trebago gives 1001 and 1000, giving BOTH
- never re-derive the tables
