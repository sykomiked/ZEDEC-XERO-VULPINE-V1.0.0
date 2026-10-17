# Criteria (instantiated) — pharos

matrix 326 · wave spec `wave27/326-pharos.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `hazard_spread` (area reached over time from the declared sp | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `warning_log` (warning number, receipts counted, never reuse | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## Clauses the pattern library does not yet cover (author these)

- `priority_ok` (a warning's priority decides its channel order
- a lower one never pre-empts a higher one) in Ada as Eve + APL as KRON witnessed
- `coverage` grid (region by channel) in APL as KRON
- `regions` tree in MUMPS as MYRA
