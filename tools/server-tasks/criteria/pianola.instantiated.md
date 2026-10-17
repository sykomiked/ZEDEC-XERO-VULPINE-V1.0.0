# Criteria (instantiated) — pianola

matrix 354 · wave spec `wave29/354-pianola.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `contract`: (critical) contracts and limits per function ( | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `kernels`: numeric kernels under test (sums and rationals) | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| - `kernels`: numeric kernels under test (sums and rationals) | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| - `billing`: release billing (build minutes and fees in mino | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| the test asserts state 3 and that it is shown, and `ship_ok` | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `ship_ok`: (critical) "may ship" (every pillar's witnessed run agrees AND Fulgurite's `may sign` TRUE AND the owner approved) in Ada as Eve + APL as KRON witnessed, joined with `lpres_conjoin` in C
- - `tests`: test arrays (functions x languages x rows, disagreements found) in APL as KRON
- - `run_jobs`: test run order (modules after their dependencies) in APL as KRON
- - `modules`: module tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the Sutra and Trebago versions of one function return 1001 and 1000 on the same row: the function is BOTH
- never re-derive the tables
