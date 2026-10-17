# Criteria (instantiated) — macrame

matrix 396 · wave spec `wave33/396-macrame.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `cap_ok`: (critical) bandwidth caps (each member within th | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `cap_ok`: (critical) bandwidth caps (each member within th | `share_x100` | num | `part,whole` | kron, trebago | the part's share of the whole in hundredths of a percent, floor: part  | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| - `costs`: cost sharing (largest remainder, minor units, ref | `largest_remainder` | num | `pool,w1,w2,w3,w4` | sutra, trebago | the first recipient's share of the pool by weight (floor), plus one wh | `check largest_remainder pool=100,10,7,100 w1=1,1,1,1 w2=1,1,1,1 w3=1,1,1,1 w4=1,0,0,0 = 25,4,3,34` |
| - `costs`: cost sharing (largest remainder, minor units, ref | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| the test asserts state 3 and that traffic keeps flowing on t | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `routes`: (critical) routing metrics (expected transmissions per link, shortest path over the link matrix) in FORTRAN as Trebago + APL as KRON witnessed
- - `notice`: route change notices (who hears about a failed link) in Ada as Eve
- - `links`: link matrix in APL as KRON
- - `nodes`: node tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two nodes report different routes to one peer: the route is BOTH
- never re-derive the tables
