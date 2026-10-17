# Criteria (instantiated) — stereoscope

matrix 350 · wave spec `wave29/350-stereoscope.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `licence_sale`: (critical) licence sale (price per use kin | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| - `exposure_ok`: exposure limits (shutter, ISO within the ca | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `pixels`: pixel arrays (edits as whole-row operations) in  | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| Paraconsistent (BOTH test, in test_zxapp.c): an edit and its | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |
| the test asserts state 3 and that viewers see the edited mar | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `edited_mark`: (critical) "show as edited" (the edit's content id differs from the signed original's) in Ada as Eve + APL as KRON witnessed
- - `colour`: colour science (white balance gains, integer 3x3 matrices q12) in FORTRAN as Trebago
- - `albums`: shoot album tree in MUMPS as MYRA
- never re-derive the tables
