# Criteria (instantiated) — atelier

matrix 393 · wave spec `wave33/393-atelier.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `publish_ok`: (critical) contract and capability checks (e | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `budget`: (critical) budget totals (the app's composed bud | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `budget`: (critical) budget totals (the app's composed bud | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| - `parse_check`: manifest line check before publishing (coun | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| the test asserts state 3 and that the maker sees both before | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- wasm owns sandboxing here)
- - `blocks`: block grid in APL as KRON
- - `ledger`: publication ledger (publications and versions, refusing on overflow) in COBOL as Sutra
- - `apps`: app tree in MUMPS as MYRA
- - The C++ wasm block is built with `tools/apps/build_wasm_blocks.sh` and the .wasm committed with it
- Paraconsistent (BOTH test, in test_zxapp.c): two blocks inside a new app disagree on a wire: the app is BOTH
- never re-derive the tables
