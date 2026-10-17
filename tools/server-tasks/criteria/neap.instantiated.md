# Criteria (instantiated) — neap

matrix 234 · wave spec `wave19/234-neap.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - (critical) "entry may post" (debits equal credits, every a | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - (critical) "entry may post" (debits equal credits, every a | `flags_none` | truth | `bits,forbidden` | eve | true when (bits & forbidden) == 0 (no refused bit set) | `check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false` |
| - account grid (accounts x months) in APL as KRON | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - chart-of-accounts tree in MUMPS as MYRA | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| Paraconsistent (BOTH test, in test_zxapp.c): a statement lin | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |
| the test (scratch books) asserts state 3, the amount on a su | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| the test (scratch books) asserts state 3, the amount on a su | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - (critical) A = L + E after every post in COBOL as Sutra + FORTRAN as Trebago witnessed
- - depreciation (straight line and declining balance in minor units, the remainder in the last period) in FORTRAN as Trebago + COBOL as Sutra witnessed
- - general ledger lines refusing on overflow in COBOL as Sutra
- never re-derive the tables
