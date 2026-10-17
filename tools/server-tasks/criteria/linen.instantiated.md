# Criteria (instantiated) — linen

matrix 363 · wave spec `wave30/363-linen.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `credits`: (critical) carbon credits (tonnes CO2e x1000 to | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| - `chem_ok`: chemical limits in Ada as Eve | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| the test (scratch books) asserts state 3 and that credits wa | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `credit_ok`: (critical) "issue credits" (carbon gain measured on the ground AND by the second method within the declared margin AND verified by a named verifier) in Ada as Eve + APL as KRON witnessed
- - `growth`: growth models (biomass gain per year by species table) in FORTRAN as Trebago
- - `fields`: field grid in APL as KRON
- - `plots`: plot tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): satellite estimate 42 t and ground plots 31 t for one stand: the gain is BOTH
- never re-derive the tables
