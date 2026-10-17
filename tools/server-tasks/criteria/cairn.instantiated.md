# Criteria (instantiated) — cairn

matrix 271 · wave spec `wave22/271-cairn.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - Sutra is left out: no money or counted business records | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| - Sutra is left out: no money or counted business records | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| the test asserts state 3 and that both stand until the arbit | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - (critical) vote tallies (yes, no, abstain per proposal) in FORTRAN as Trebago + APL as KRON witnessed
- - (critical) "decision carried" (quorum met AND the majority threshold) in Ada as Eve + APL as KRON witnessed
- - ballot matrix (members x proposals) in APL as KRON
- - member register and council tree in MUMPS as MYRA
- the register is MYRA's
- Paraconsistent (BOTH test, in test_zxapp.c): two rulings on one matter contradict, giving BOTH
- never re-derive the tables
