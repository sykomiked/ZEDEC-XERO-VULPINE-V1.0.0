# Criteria (instantiated) — sedge

matrix 248 · wave spec `wave20/248-sedge.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - (critical) "may take now" (the last 24 hours plus this dos | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - (critical) "may take now" (the last 24 hours plus this dos | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| - interaction matrix (medicines x medicines, flags from the  | `flags_none` | truth | `bits,forbidden` | eve | true when (bits & forbidden) == 0 (no refused bit set) | `check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false` |
| - stock records (doses left and days of stock, refusing belo | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| - stock records (doses left and days of stock, refusing belo | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| the test asserts state 3, both shown, and a prompt to check  | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - (critical) next dose time (minutes from the last dose and the interval) in FORTRAN as Trebago + APL as KRON witnessed
- - patient tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): source A lists no interaction and source B a major one for the same pair, giving BOTH
- never re-derive the tables
