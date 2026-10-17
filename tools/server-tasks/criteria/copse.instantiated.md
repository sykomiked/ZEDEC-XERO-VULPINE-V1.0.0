# Criteria (instantiated) — copse

matrix 235 · wave spec `wave19/235-copse.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - (critical) cut plan (parts packed onto boards, longest fir | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - (critical) "within tolerance" (part and joint sizes within | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| - material cost (volume or length x price, minor units, refu | `within_range` | truth | `value,min,max` | eve, kron | true when min <= value <= max | `check within_range value=0,50,100,101 min=0,0,0,0 max=100,100,100,100 = true,true,true,false` |
| - material cost (volume or length x price, minor units, refu | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| the test asserts state 3 and that the cuts on that board are | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - cut grid (boards x parts) in APL as KRON
- - assembly tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): one board measured at 1203 mm and again at 1198 mm with a 2 mm tolerance gives BOTH
- never re-derive the tables
