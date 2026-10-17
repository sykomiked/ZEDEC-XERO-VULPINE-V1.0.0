# Criteria (instantiated) — spinnaker

matrix 366 · wave spec `wave30/366-spinnaker.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `alt_ok`: (critical) altitude limits (planned altitude wit | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `alt_ok`: (critical) altitude limits (planned altitude wit | `grade_band` | num | `value,b1,b2,b3,b4` | kron, trebago | the band index 0..4 of the value across the four ascending bounds | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| - `fees`: landing fees (minor units, refusing on overflow) i | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| the test asserts state 3 and a crosscheck prompt, neither tr | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `wb_ok`: (critical) weight and balance (total mass and moment within the envelope) in Ada as Eve + APL as KRON witnessed
- - `dynamics`: flight dynamics (take-off distance and climb from integer performance tables by mass and density altitude) in FORTRAN as Trebago
- - `airspace`: airspace grid in APL as KRON
- - `checklist`: checklist sequencing (items in order, none skipped) in Ada as Eve
- - `airframe`: airframe tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two altimeters read 1200 ft and 1450 ft: altitude is BOTH
- never re-derive the tables
