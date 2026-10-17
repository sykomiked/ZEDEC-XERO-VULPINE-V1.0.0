# Criteria (instantiated) — planisphere

matrix 391 · wave spec `wave32/391-planisphere.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| any BOTH keeps the last good state) in Ada as Eve + APL as K | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |
| - `capacity`: (critical) capacity planning (disc, memory and | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `inventory`: machine inventory (parts x status) in APL as  | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |
| - `config`: configuration tree in MUMPS as MYRA | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| the test asserts state 3 and that the machine stays on its l | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `step_ok`: (critical) "do this step" (its part's check TRUE AND every earlier step done AND the owner confirmed
- - `history`: machine history (signed steps, refusing on overflow) in COBOL as Sutra
- Paraconsistent (BOTH test, in test_zxapp.c): Fusee passes a boot image and Thicket's auditors disagree on the update inside it: the step is BOTH
- never re-derive the tables
