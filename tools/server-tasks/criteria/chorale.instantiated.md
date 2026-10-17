# Criteria (instantiated) — chorale

matrix 407 · wave spec `wave34/407-chorale.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `quorum_ok`: (critical) quorum limits (quorum met AND the  | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `budget`: (critical) public budget lines (minor units, ref | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| the test asserts state 3 and that the assembly continues unt | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `turnout`: turnout model (turnout per ward per vote) exact in FORTRAN as Trebago
- - `ballots`: ballot arrays in APL as KRON
- - `decisions`: published decision layout (motion, result, votes) in APL as KRON
- - `members`: member tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two motions passed on the same matter contradict: both stand as BOTH
- never re-derive the tables
