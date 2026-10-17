# Criteria (instantiated) — thaw

matrix 266 · wave spec `wave22/266-thaw.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - (critical) restore byte totals (refusing on overflow) in C | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| - restore time estimate (bytes / measured rate) in FORTRAN a | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| the test (scratch Hoard) asserts state 3 and that both are r | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - (critical) "may restore this item" (`backup.verify` ok AND chain intact AND it fits) in Ada as Eve + APL as KRON witnessed
- - block map (items x restored) in APL as KRON
- - system tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two backups of one folder disagree, giving BOTH
- never re-derive the tables
