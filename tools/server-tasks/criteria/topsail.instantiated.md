# Criteria (instantiated) — topsail

matrix 399 · wave spec `wave33/399-topsail.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `cap_ok`: (critical) resource caps (each tenant within its | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `cap_ok`: (critical) resource caps (each tenant within its | `share_x100` | num | `part,whole` | kron, trebago | the part's share of the whole in hundredths of a percent, floor: part  | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| - `capacity`: (critical) capacity model (bytes and replicas  | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| Paraconsistent (BOTH test, in test_zxapp.c): two replicas di | `share_x100` | num | `part,whole` | kron, trebago | the part's share of the whole in hundredths of a percent, floor: part  | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| Paraconsistent (BOTH test, in test_zxapp.c): two replicas di | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| the test asserts state 3 and that both are kept until reconc | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `failover`: failover choice (next replica by health and recency) in Ada as Eve
- - `replicas`: replica grid in APL as KRON
- - `ledger`: hosting ledger (refusing on overflow) in COBOL as Sutra
- - `tenants`: tenant tree in MUMPS as MYRA
- never re-derive the tables
