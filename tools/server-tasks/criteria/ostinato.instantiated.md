# Criteria (instantiated) — ostinato

matrix 371 · wave spec `wave31/371-ostinato.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| conserved exactly) in FORTRAN as Trebago | `conserved_sum` | truth | `whole,a,b,c,d` | trebago, sutra | true when a + b + c + d == whole exactly (nothing created or destroyed | `check conserved_sum whole=100,100,100,101 a=25,0,0,25 b=25,0,0,25 c=25,0,0,25 d=25,100,99,25 = true,true,false,false` |
| - `hazard_ok`: hazard limits (batteries and chemicals routed | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `lineage`: product lineage tree in MUMPS as MYRA | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| the test (scratch books) asserts state 3 and the deposit esc | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `refund_ok`: (critical) "refund the deposit" (item returned AND inspected AND not reported in use) in Ada as Eve + COBOL as Sutra witnessed
- - `deposit`: (critical) deposit refunds (minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- - `flow`: material flow model (grams in, reused, recycled, lost
- - `materials`: material grid in APL as KRON
- Paraconsistent (BOTH test, in test_zxapp.c): an item is reported returned at the depot and in use by its holder: it is BOTH
- never re-derive the tables
