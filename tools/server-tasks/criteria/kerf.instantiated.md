# Criteria (instantiated) — kerf

matrix 290 · wave spec `wave24/290-kerf.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `tolerance_stack` (sum of the parts' tolerances along an ass | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `job_costing` (materials, machine minutes and labour in mino | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## Clauses the pattern library does not yet cover (author these)

- `machine_ok` (a job starts on a machine only with its guards closed, its service not overdue and an operator signed in) in Ada as Eve + APL as KRON witnessed
- `schedule` grid (machine by hour) in APL as KRON
- `bom` tree in MUMPS as MYRA
