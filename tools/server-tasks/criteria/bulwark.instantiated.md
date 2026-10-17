# Criteria (instantiated) — bulwark

matrix 375 · wave spec `wave31/375-bulwark.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `transfer_ok`: (critical) withdrawal limits (k of n guardi | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `transfer_ok`: (critical) withdrawal limits (k of n guardi | `modular_draw` | num | `seed,members,step` | trebago, sutra | the sortition's modular walk: (seed * 1103515245 + 12345 + step * 7) m | `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6` |
| - `risk`: (critical) risk score (exposure, guardian spread,  | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| the test (scratch books) asserts state 3 and the transfer fr | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `access`: access matrix (people x assets) in APL as KRON
- - `register`: asset register (minor units, refusing on overflow) in COBOL as Sutra
- - `roles`: role hierarchy and approval layers in Ada as Eve
- - `guardians`: guardian tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two guardians approve and one vetoes the same transfer: it is BOTH
- never re-derive the tables
