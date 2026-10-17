# Criteria (instantiated) — tatting

matrix 372 · wave spec `wave31/372-tatting.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `deliver_ok`: (critical) "deliver" (every log reading with | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| - `deliver_ok`: (critical) "deliver" (every log reading with | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - `decay`: (critical) thermal decay (remaining shelf minutes | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| the test asserts state 3 and that it is quarantined, neither | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `sensors`: sensor arrays (loggers x minutes) in APL as KRON
- - `alert`: spoilage alert routing (consignment to its handlers) in Ada as Eve
- - `billing`: shipment billing (minor units, refusing on overflow) in COBOL as Sutra
- - `consignments`: consignment tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two loggers in one box disagree on an excursion: the shipment is BOTH
- never re-derive the tables
