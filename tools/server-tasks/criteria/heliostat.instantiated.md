# Criteria (instantiated) — heliostat

matrix 402 · wave spec `wave33/402-heliostat.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `inverter_ok`: (critical) inverter limits (export at most  | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `inverter_ok`: (critical) inverter limits (export at most  | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - `credits`: (critical) energy credits (surplus Wh x price p | `largest_remainder` | num | `pool,w1,w2,w3,w4` | sutra, trebago | the first recipient's share of the pool by weight (floor), plus one wh | `check largest_remainder pool=100,10,7,100 w1=1,1,1,1 w2=1,1,1,1 w3=1,1,1,1 w4=1,0,0,0 = 25,4,3,34` |
| - `credits`: (critical) energy credits (surplus Wh x price p | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| the test (scratch books) asserts state 3 and the credits on  | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- settled only on the owners' approval
- - `irradiance`: irradiance model (Wh per panel from sun minutes and tilt table) in FORTRAN as Trebago
- - `settle_jobs`: settlement schedule (end of each declared period) in Ada as Eve
- - `panels`: panel output grid in APL as KRON
- - `households`: household tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the meter says 12.4 kWh exported and the inverter 10.9: the export is BOTH
- never re-derive the tables
