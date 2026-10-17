# Criteria (instantiated) — quadrant

matrix 394 · wave spec `wave33/394-quadrant.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `tx_ok`: (critical) transmit power limits (at most the lic | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `tx_ok`: (critical) transmit power limits (at most the lic | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - `report`: pass log summaries (passes, bytes per station) i | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| the test asserts state 3 and that both are kept, nothing sil | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `orbit`: (critical) pass prediction (rise, culmination and set minutes from integer orbital elements) in FORTRAN as Trebago + APL as KRON witnessed
- - `relay`: message relay plan (pass by pass) in Ada as Eve
- - `imagery`: imagery arrays in APL as KRON
- - `billing`: pass billing (minor units, refusing on overflow) in COBOL as Sutra
- - `stations`: station tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): one relayed message arrives in two different forms: it is BOTH
- never re-derive the tables
