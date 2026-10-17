# Criteria (instantiated) — crowsnest

matrix 358 · wave spec `wave30/358-crowsnest.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `mask_ok`: (critical) privacy masking (frames outside the  | `flags_none` | truth | `bits,forbidden` | eve | true when (bits & forbidden) == 0 (no refused bit set) | `check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false` |
| - `motion`: (critical) motion detection (changed pixels per  | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| the test asserts state 3 and that it is logged and escalated | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `escalate`: escalation (two sensors agree, or BOTH, goes to a person) in Ada as Eve
- - `frames`: frame arrays in APL as KRON
- - `billing`: incident billing (callouts in minor units, refusing on overflow) in COBOL as Sutra
- - `sites`: site tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the door sensor reports entry and the camera shows no motion: the event is BOTH
- never re-derive the tables
