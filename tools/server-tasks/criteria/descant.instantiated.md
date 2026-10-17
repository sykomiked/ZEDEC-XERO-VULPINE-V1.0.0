# Criteria (instantiated) — descant

matrix 397 · wave spec `wave33/397-descant.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `pace_ok`: pace limits (lessons per day within the learner | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `records`: credential records (refusing on overflow) in CO | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| the test asserts state 3 and that both are recorded and no c | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| the test asserts state 3 and that both are recorded and no c | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `credential_ok`: (critical) "issue this credential" (every required skill mastered AND a teacher signed) in Ada as Eve + APL as KRON witnessed
- - `mastery`: (critical) mastery estimate (right answers in a row and spaced reviews, integer score) in FORTRAN as Trebago + APL as KRON witnessed
- - `steps`: the next step shown one at a time in BASIC (twin Hatchling)
- - `skills`: skill grid in APL as KRON
- - `tree`: learning tree in MUMPS as MYRA
- - Forth and BASIC blocks are committed in twin form: the manifest's logic line names the twin (`logic cairndrake ...`, `logic hatchling ...`)
- Paraconsistent (BOTH test, in test_zxapp.c): the tests mark a skill mastered and the teacher does not: it is BOTH
- never re-derive the tables
