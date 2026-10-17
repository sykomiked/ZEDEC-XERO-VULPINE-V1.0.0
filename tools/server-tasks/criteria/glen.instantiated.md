# Criteria (instantiated) — glen

matrix 220 · wave spec `wave18/220-glen.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - (critical) "this child rule takes effect" = within the chi | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - child limits (minutes 0..1440, bedtime as minutes of day)  | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - Sutra is left out: Glen keeps no money or counted business | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| - Sutra is left out: Glen keeps no money or counted business | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| household records are MYRA's | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| the test asserts state 3, both shown for agreement, and the  | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - schedule load (minutes booked per person per day) exact in FORTRAN as Trebago
- - availability grid (family members x 15-minute slots, the common free times) in APL as KRON
- - family tree (members, guardians, children, contacts) in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): parent A sets 60 minutes and parent B 90 minutes for the same child on the same day, giving BOTH
- never re-derive the tables
