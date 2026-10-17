# Criteria (instantiated) — carapace

matrix 383 · wave spec `wave32/383-carapace.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `pay_ok`: (critical) "pay this landing" (log and landing w | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| - `payment`: (critical) landing payments (kg x price per spe | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| - `labels`: traceable label layout (vessel, area, date, spec | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| the test (scratch books) asserts state 3 and the payment hel | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `stocks`: fish stock models (landings and effort per season, catch per unit effort) in FORTRAN as Trebago
- - `catch`: catch grid in APL as KRON
- - `vessels`: vessel tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the catch log says 410 kg and the landing scale 352 kg: the landing is BOTH
- never re-derive the tables
