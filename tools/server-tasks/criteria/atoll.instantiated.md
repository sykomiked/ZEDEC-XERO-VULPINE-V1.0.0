# Criteria (instantiated) — atoll

matrix 258 · wave spec `wave21/258-atoll.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - (critical) catch sale value (kg x price, minor units, refu | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| the test asserts state 3 and that both are kept for quota re | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - (critical) "may land this catch" (season total plus this catch within the quota, size at least the minimum, zone open) in Ada as Eve + APL as KRON witnessed
- - tide height in mm from an integer table of constituents in FORTRAN as Trebago
- - zone grid (zones x open, closed) in APL as KRON
- - species tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two crew catch reports for one haul differ, giving BOTH
- never re-derive the tables
