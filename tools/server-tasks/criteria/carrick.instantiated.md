# Criteria (instantiated) — carrick

matrix 360 · wave spec `wave30/360-carrick.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `limit_ok`: (critical) limit clauses (a payment or penalty | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `limit_ok`: (critical) limit clauses (a payment or penalty | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - `penalty`: (critical) penalty schedules (days late x rate, | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `penalty`: (critical) penalty schedules (days late x rate, | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| - `matrix`: obligation matrix (parties x obligations, four v | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| the test (scratch books) asserts state 3 and that only those | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `ledger`: obligation ledger (due, paid, held, refusing on overflow) in COBOL as Sutra
- - `parties`: party tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): obligation 3 requires delivery by 1 May and obligation 7 forbids it before 10 May: the pair is BOTH
- never re-derive the tables
