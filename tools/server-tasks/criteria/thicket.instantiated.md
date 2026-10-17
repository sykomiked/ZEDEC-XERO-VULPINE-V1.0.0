# Criteria (instantiated) — thicket

matrix 262 · wave spec `wave21/262-thicket.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - proposal ledger (proposal numbers and vote counts, refusin | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| the test asserts state 3 and that it is not installed. Join  | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - (critical) "may accept" = an audit present AND the auditors agree AND tests TRUE AND both signatures valid, in Ada as Eve + APL as KRON witnessed
- - (critical) test score x100 (weighted passes per suite) in FORTRAN as Trebago + APL as KRON witnessed
- - review matrix (auditors x checks) in APL as KRON
- - fork tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): auditor A passes an update and auditor B fails it, giving BOTH
- never re-derive the tables
