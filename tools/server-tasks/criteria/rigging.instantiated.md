# Criteria (instantiated) — rigging

matrix 381 · wave spec `wave32/381-rigging.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `deploy_ok`: (critical) budget and rate limits (each job w | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `deploy_ok`: (critical) budget and rate limits (each job w | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| the test asserts state 3 and that only that branch pauses wh | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `forecast`: (critical) load forecast (next hour's load from the last n hours, integer least squares) in FORTRAN as Trebago + APL as KRON witnessed
- - `rollback`: rollback plan (the last version every dependant agrees on) in APL as KRON
- - `jobs`: job arrays (jobs x machines) in APL as KRON
- - `billing`: compute billing (minor units, refusing on overflow) in COBOL as Sutra
- - `services`: service tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two agents report a branch as running and stopped: the branch is BOTH
- never re-derive the tables
