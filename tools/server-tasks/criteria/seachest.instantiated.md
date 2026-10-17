# Criteria (instantiated) — seachest

matrix 403 · wave spec `wave33/403-seachest.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `access_ok`: (critical) access limits (a release only when | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| the test asserts state 3 and that nothing is released until  | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `schedule`: (critical) release scheduling (dates from the will's terms, days since the trigger) in FORTRAN as Trebago + APL as KRON witnessed
- - `custody`: custody ledger (items in and out, refusing on overflow) in COBOL as Sutra
- - `items`: item grid in APL as KRON
- - `beneficiaries`: beneficiary tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two claims to one deed: it is BOTH
- never re-derive the tables
