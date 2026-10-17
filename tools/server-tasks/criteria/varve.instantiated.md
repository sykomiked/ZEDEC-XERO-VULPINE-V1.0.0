# Criteria (instantiated) — varve

matrix 243 · wave spec `wave20/243-varve.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - source rules (a source counts when dated and cited | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - Sutra is left out: no money or counted business records | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| - Sutra is left out: no money or counted business records | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| the test asserts state 3 and that both dates stay on the tim | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - (critical) date shown for an event (each source's earliest and latest day number intersected
- circa ranges kept) in FORTRAN as Trebago + APL as KRON witnessed
- primary or secondary marked) in Ada as Eve
- - event matrix (events x sources) in APL as KRON
- - archive catalogue and era tree in MUMPS as MYRA
- the catalogue is MYRA's
- Paraconsistent (BOTH test, in test_zxapp.c): source A dates an event to 1066 and source B to 1067 with no overlap, giving BOTH
- never re-derive the tables
