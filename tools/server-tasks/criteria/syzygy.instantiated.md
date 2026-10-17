# Criteria (instantiated) — syzygy

matrix 303 · wave spec `wave25/303-syzygy.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `penalty` (days late x the clause's daily rate, capped at th | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `penalty` (days late x the clause's daily rate, capped at th | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |

## Clauses the pattern library does not yet cover (author these)

- `deadline_ok` (an obligation met on or before its deadline day) in Ada as Eve + APL as KRON witnessed
- `contract_payments` (in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `clauses` matrix in APL as KRON
- `parties` hierarchy in MUMPS as MYRA
