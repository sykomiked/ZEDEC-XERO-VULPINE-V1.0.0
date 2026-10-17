# Criteria (instantiated) — fulgurite

matrix 245 · wave spec `wave20/245-fulgurite.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - (critical) "reproducible" (two builds' artefact byte vecto | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - build timing (stage ms, totals) exact in FORTRAN as Trebag | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| - release ledger (release numbers and artefact byte totals,  | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| the test uses two fixed image files under build/ differing a | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - (critical) "may sign" = reproducible AND every test TRUE (joined with `lpres_conjoin` in C) AND the mutation gate passed AND the owner approved, in Ada as Eve + APL as KRON witnessed
- - (critical) "refuse an unpinned download" (every dependency names a sha256 content id) in Ada as Eve + APL as KRON witnessed
- - bill-of-materials tree (component, version, licence, CID) in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two builds of the same source whose images differ in one byte give BOTH
- never re-derive the tables
