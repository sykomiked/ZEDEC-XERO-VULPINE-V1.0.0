# Criteria (instantiated) — theorbo

matrix 348 · wave spec `wave29/348-theorbo.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `split_ok`: (critical) "royalty split agreed" (every contr | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| - `split_ok`: (critical) "royalty split agreed" (every contr | `share_x100` | num | `part,whole` | kron, trebago | the part's share of the whole in hundredths of a percent, floor: part  | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| - `mix_level`: (critical) loudness of the final mix (integra | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `mix_level`: (critical) loudness of the final mix (integra | `grade_band` | num | `value,b1,b2,b3,b4` | kron, trebago | the band index 0..4 of the value across the four ascending bounds | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| the test asserts state 3 and that both takes stay live in th | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `synth`: audio synthesis for click and guide tones (integer sine table, samples per period) in FORTRAN as Trebago
- - `score`: score arrays (bars x parts x events) in APL as KRON
- - `versions`: song version tree (take, stem, editor) in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two collaborators keep different takes of bar 9 to 16: the passage is BOTH
- never re-derive the tables
