# Criteria (instantiated) — rainshadow

matrix 261 · wave spec `wave21/261-rainshadow.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - Sutra is left out: no money or counted business records he | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| - Sutra is left out: no money or counted business records he | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| the test asserts state 3 and that both times are shown with  | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - (critical) "safe to look without a filter" (only inside the totality window at this place
- every partial phase needs a certified filter) in Ada as Eve + APL as KRON witnessed
- - (critical) contact times at this place (seconds) from the integer ephemeris tables in FORTRAN as Trebago + APL as KRON witnessed
- - ephemeris grid (events x places) in APL as KRON
- - observation log and event tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): model A gives first contact at 14:03:20 and model B at 14:04:10, giving BOTH
- never re-derive the tables
