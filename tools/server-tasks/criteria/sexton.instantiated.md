# Criteria (instantiated) — sexton

matrix 195 · wave spec `wave16/195-sexton.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| incident record (incident number, opened, acknowledged, clos | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| alert matrix (monitor by state) in APL as KRON | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- "escalate" (no acknowledgement within the response deadline) in Ada as Eve + APL as KRON witnessed
- impact (seats affected x minutes) exact in FORTRAN as Trebago
- on-call tree in MUMPS as MYRA
