# Criteria (instantiated) — brocade

matrix 376 · wave spec `wave31/376-brocade.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `size_ok`: (critical) size limits (each measurement within | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `fabric`: (critical) fabric yield (pieces laid on the widt | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| - `labels`: care and size label text checks (bounded fields) | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| - `labels`: care and size label text checks (bounded fields) | `clamp` | num | `value,min,max` | kron, trebago | value clamped into [min,max] | `check clamp value=-5,0,50,150 min=0,0,0,0 max=100,100,100,100 = 0,0,50,100` |
| the test asserts state 3 and that the tailor is asked before | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `orders`: order ledger (minor units, refusing on overflow) in COBOL as Sutra
- - `colourways`: colourway grid in APL as KRON
- - `collection`: collection tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): a fitting note says the waist is too tight while the measurement is within ease: the fit is BOTH
- never re-derive the tables
