# Criteria (instantiated) — serac

matrix 260 · wave spec `wave21/260-serac.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - (critical) the 0.08889% assurance fee (amount x 8889 / 10, | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| - (critical) the 0.08889% assurance fee (amount x 8889 / 10, | `share_x100` | num | `part,whole` | kron, trebago | the part's share of the whole in hundredths of a percent, floor: part  | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| - account records in MUMPS as MYRA | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - account records in MUMPS as MYRA | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| Paraconsistent (BOTH test, in test_zxapp.c): `vault.status`  | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |
| the test (features off) asserts state 3 and that the dividen | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - (critical) "reserve at or above the floor" (reserve x floor_den >= floor_num x (N - h) x backing, svault.h S4) in Ada as Eve + COBOL as Sutra witnessed
- - epoch tables (epochs x reserve, pool, dividend) in APL as KRON
- - reserve ledger lines refusing on overflow in COBOL as Sutra
- never re-derive the tables
