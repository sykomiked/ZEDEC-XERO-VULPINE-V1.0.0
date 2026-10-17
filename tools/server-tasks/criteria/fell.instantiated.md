# Criteria (instantiated) — fell

matrix 253 · wave spec `wave21/253-fell.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - (critical) sector priority (probability of area x10000 per | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| - (critical) sector priority (probability of area x10000 per | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| - (critical) crew safety (shift at most 12 hours, check-in a | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - callout records (crew hours and mileage claims in minor un | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| the test asserts state 3 and that neither sector is dropped. | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- a suggestion for the incident commander
- - sector grid in APL as KRON, and the board's filters (unsearched sectors, sightings by time) as K queries in APL as KRON
- - casualty tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): a sighting in sector 4 at 14:10 and another in sector 9 at 14:12 give BOTH
- never re-derive the tables
