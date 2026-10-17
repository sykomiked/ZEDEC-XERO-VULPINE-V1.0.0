# Criteria (instantiated) — caldera

matrix 259 · wave spec `wave21/259-caldera.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - hazard grid (region cells x hazard levels) in APL as KRON | `grade_band` | num | `value,b1,b2,b3,b4` | kron, trebago | the band index 0..4 of the value across the four ascending bounds | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| - incident records (incident numbers and counts, refusing on | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - incident records (incident numbers and counts, refusing on | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| the test asserts state 3 and a "prepare" warning, never an a | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - (critical) "raise a local alert" (a reading at or above the evacuation threshold for this region) in Ada as Eve + APL as KRON witnessed, held for a person's confirm
- - (critical) local magnitude x10 from amplitude and distance by table in FORTRAN as Trebago + APL as KRON witnessed
- - region tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): sensor A raises an alert and sensor B shows none, giving BOTH
- never re-derive the tables
