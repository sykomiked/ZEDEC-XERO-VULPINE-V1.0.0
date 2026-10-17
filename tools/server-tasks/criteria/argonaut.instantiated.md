# Criteria (instantiated) — argonaut

matrix 367 · wave spec `wave30/367-argonaut.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `withdraw_ok`: (critical) withdrawal limits (an emergency  | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `withdraw_ok`: (critical) withdrawal limits (an emergency  | `modular_draw` | num | `seed,members,step` | trebago, sutra | the sortition's modular walk: (seed * 1103515245 + 12345 + step * 7) m | `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6` |
| Paraconsistent (BOTH test, in test_zxapp.c): the bank statem | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |
| the test (scratch books) asserts state 3 and the 50.00 held, | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- a future chamber only after its date) in Ada as Eve + COBOL as Sutra witnessed
- - `compound`: (critical) integer compound growth per chamber (balance x (den + num) / den per period, floor, remainder carried) in FORTRAN as Trebago + COBOL as Sutra witnessed
- - `ledger`: savings ledger (minor units, refusing on overflow) in COBOL as Sutra
- - `goals`: goal grid (chambers x months) in APL as KRON
- - `household`: household tree in MUMPS as MYRA
- never re-derive the tables
