# Criteria (instantiated) — commonweal

matrix 387 · wave spec `wave32/387-commonweal.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `advisory_ok`: (critical) advisory limits (Commonweal post | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `shares`: (critical) your share of the dividend (your weig | `share_x100` | num | `part,whole` | kron, trebago | the part's share of the whole in hundredths of a percent, floor: part  | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| - `shares`: (critical) your share of the dividend (your weig | `largest_remainder` | num | `pool,w1,w2,w3,w4` | sutra, trebago | the first recipient's share of the pool by weight (floor), plus one wh | `check largest_remainder pool=100,10,7,100 w1=1,1,1,1 w2=1,1,1,1 w3=1,1,1,1 w4=1,0,0,0 = 25,4,3,34` |
| the test (features off) asserts state 3 and that both are sh | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| the test (features off) asserts state 3 and that both are sh | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- every action it suggests is shown as a step the owner takes in Coffer, Gavel or Crown) in Ada as Eve + APL as KRON witnessed
- - `household`: household ledger view (grants, bounties, routes, dividend per epoch, refusing on overflow) in COBOL as Sutra
- - `epochs`: epoch tables in APL as KRON
- - `holdings`: holdings tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): your books and `mesh.books` disagree on last epoch's bounties: the figure is BOTH
- never re-derive the tables
