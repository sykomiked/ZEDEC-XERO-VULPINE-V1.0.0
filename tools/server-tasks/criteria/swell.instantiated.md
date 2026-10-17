# Criteria (instantiated) — swell

matrix 251 · wave spec `wave20/251-swell.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - (critical) click time per device (start + n x 60000000 / b | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| - (critical) latency limits (jitter at most 30 ms, else the  | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - (critical) latency limits (jitter at most 30 ms, else the  | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - Sutra is left out: no money or counted business records | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| - Sutra is left out: no money or counted business records | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| the test asserts state 3 and that both edits are kept until  | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - score matrix (bars x parts) in APL as KRON
- - player roster and part tree in MUMPS as MYRA
- the roster is MYRA's
- Paraconsistent (BOTH test, in test_zxapp.c): the conductor's and the section leader's edits to bar 12 differ, giving BOTH
- never re-derive the tables
