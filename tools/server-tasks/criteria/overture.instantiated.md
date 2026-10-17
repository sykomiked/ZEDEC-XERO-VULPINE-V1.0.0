# Criteria (instantiated) — overture

matrix 252 · wave spec `wave20/252-overture.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - setup log as fixed records (step, result), the step counte | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - setup log as fixed records (step, result), the step counte | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| - account records in MUMPS as MYRA | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - account records in MUMPS as MYRA | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| the test asserts state 3 and that both are shown to the user | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - (critical) "this seat may be used" = the licence accepted by this user (never inherited from another seat) in Ada as Eve + APL as KRON witnessed
- - (critical) local time from UTC minutes and the zone's offset and daylight rule in FORTRAN as Trebago + APL as KRON witnessed
- - language and layout tables (language x layout x script) in APL as KRON
- Paraconsistent (BOTH test, in test_zxapp.c): the old machine's text size 18 and the new machine's 14 give BOTH
- never re-derive the tables
