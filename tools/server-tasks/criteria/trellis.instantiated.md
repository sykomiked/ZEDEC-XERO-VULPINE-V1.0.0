# Criteria (instantiated) — trellis

matrix 29 · wave spec `wave1/029-trellis.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| invitation minutes left in kron | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| peers per state (active, revoked, waiting) in kron + trebago | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |
| peer record in myra | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- "may this device be revoked" (admin session, not this device, not the last admin) in eve + kron witnessed
- A revocation can never be undone: the owner confirms it twice. Tests never go online
