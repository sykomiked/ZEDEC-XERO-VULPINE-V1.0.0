# Criteria (instantiated) — hornpipe

matrix 384 · wave spec `wave32/384-hornpipe.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `swim_ok`: (critical) cold and distance limits (water temp | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `swim_ok`: (critical) cold and distance limits (water temp | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - `fees`: club fees (minor units, refusing on overflow) in C | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| Paraconsistent (BOTH test, in test_zxapp.c): the watch recor | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| the test asserts state 3 and that both stand until reviewed. | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `stroke`: (critical) stroke analysis (strokes per length, distance per stroke, exact) in FORTRAN as Trebago + APL as KRON witnessed
- - `laps`: lap arrays in APL as KRON
- - `swimmers`: swimmer tree in MUMPS as MYRA
- never re-derive the tables
