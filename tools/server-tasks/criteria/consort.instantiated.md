# Criteria (instantiated) — consort

matrix 392 · wave spec `wave33/392-consort.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `contact_ok`: (critical) contact limits (an introduction o | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `contact_ok`: (critical) contact limits (an introduction o | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - `match`: (critical) match scoring (shared skills and avail | `share_x100` | num | `part,whole` | kron, trebago | the part's share of the whole in hundredths of a percent, floor: part  | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| - `network`: network tree in MUMPS as MYRA | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| the test asserts state 3 and that both are shown with their  | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `intro`: introduction routing (who is told what) in Ada as Eve
- - `skills`: skill matrix in APL as KRON
- - `contrib`: contribution ledger (refusing on overflow) in COBOL as Sutra
- Paraconsistent (BOTH test, in test_zxapp.c): two references about one person conflict: it is BOTH
- never re-derive the tables
