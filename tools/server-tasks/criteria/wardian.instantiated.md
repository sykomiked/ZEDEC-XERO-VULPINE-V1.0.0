# Criteria (instantiated) — wardian

matrix 405 · wave spec `wave34/405-wardian.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `climate_ok`: (critical) humidity and CO2 limits (per cent | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `climate_ok`: (critical) humidity and CO2 limits (per cent | `grade_band` | num | `value,b1,b2,b3,b4` | kron, trebago | the band index 0..4 of the value across the four ascending bounds | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| - `sales`: produce sales (minor units, refusing on overflow) | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| the test asserts state 3 and that control steers cautiously  | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- a step change proposed, never forced) in Ada as Eve + APL as KRON witnessed
- - `climate`: (critical) climate model (vapour pressure deficit from temperature and humidity tables) in FORTRAN as Trebago + APL as KRON witnessed
- - `growth_sim`: harvest date estimate from degree days in FORTRAN as Trebago
- - `sensors`: sensor grid in APL as KRON
- - `beds`: bed and plant tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two humidity sensors read 62% and 81%: the zone is BOTH
- never re-derive the tables
