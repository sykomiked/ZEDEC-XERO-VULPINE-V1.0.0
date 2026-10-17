# Criteria (instantiated) — fusee

matrix 247 · wave spec `wave20/247-fusee.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - (critical) slot header check at boot (magic, version, trie | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| - measurement table (expected and logged digest indexes, the | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| - boot-entry and slot ledger (tries counters per slot, refus | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| the test asserts state 3 and that the image is never booted  | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - (critical) "may boot this image" = both signature checks pass AND the stub is signed AND the version is at or above the rollback floor, in Ada as Eve + APL as KRON witnessed
- - firmware version arithmetic (major.minor.patch packed into one integer, distance to the floor) exact in FORTRAN as Trebago
- - firmware inventory tree in MUMPS as MYRA
- - Forth and BASIC blocks are committed in twin form: the manifest's logic line names the twin (`logic cairndrake ...`, `logic hatchling ...`)
- Paraconsistent (BOTH test, in test_zxapp.c): one signature check passes and the other fails on the same image, giving BOTH
- never re-derive the tables
