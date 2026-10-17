# Criteria (instantiated) — damask

matrix 355 · wave spec `wave29/355-damask.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `serve_ok`: (critical) "may serve this dish" (label allerg | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| - `portion`: portion costing (cost per plate from the scaled | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| - `fridge_ok`: fridge temperature limits (tenths of a degree | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| Paraconsistent (BOTH test, in test_zxapp.c): a dish's label  | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| the test asserts state 3 and that it is blocked from serving | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `supplier_pay`: (critical) supplier payments (invoice lines in minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- - `menu`: menu grid (dishes x services) in APL as KRON
- - `allergens`: allergen tree in MUMPS as MYRA
- never re-derive the tables
