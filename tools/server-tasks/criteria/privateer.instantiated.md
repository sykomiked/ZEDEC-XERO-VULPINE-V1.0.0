# Criteria (instantiated) — privateer

matrix 386 · wave spec `wave32/386-privateer.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `contain_ok`: (critical) containment limits (isolate only  | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `costs`: incident costs (minor units, refusing on overflow | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| the test asserts state 3 and that the item is sandboxed (hel | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- never the owner's last admin device) in Ada as Eve + APL as KRON witnessed
- - `anomaly`: (critical) anomaly score (events per minute against the baseline by kind, integer z-like score) in FORTRAN as Trebago + APL as KRON witnessed
- - `alerts`: alert routing (who hears which severity) in Ada as Eve
- - `events`: event matrix in APL as KRON
- - `assets`: asset tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): one detector calls an event benign and another malicious: it is BOTH
- never re-derive the tables
