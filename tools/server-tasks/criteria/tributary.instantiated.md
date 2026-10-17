# Criteria (instantiated) — tributary

matrix 225 · wave spec `wave18/225-tributary.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - (critical) agreeing answers counted per normalised answer  | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| the verdict (all agree TRUE, split BOTH, none NEITHER) joine | `share_x100` | num | `part,whole` | kron, trebago | the part's share of the whole in hundredths of a percent, floor: part  | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| - (critical) "may ask another model" (tokens and energy with | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - (critical) "may ask another model" (tokens and energy with | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| - query ledger (token cost per query over the D1-D9 faces of | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| the test asserts state 3 and that both answers are shown wit | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - confidence score x100 per answer (agreeing models / models asked) exact in FORTRAN as Trebago
- - answer matrix (models x answer codes) in APL as KRON
- - source tree (question, model, answer, time) in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): three models answer 42, 42 and 41 with a quorum of 3, giving BOTH
- never re-derive the tables
