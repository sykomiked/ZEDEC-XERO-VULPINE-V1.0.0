# Criteria (instantiated) — lozenge

matrix 281 · wave spec `wave23/281-lozenge.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `pollination_invoice` (hives x days x rate in minor units) i | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |

## Clauses the pattern library does not yet cover (author these)

- `placement_ok` (a hive at least the declared distance from any field sprayed in the last n days) in Ada as Eve + APL as KRON witnessed
- `colony_strength` (frames of bees and brood, weekly trend) in FORTRAN as Trebago + APL as KRON witnessed
- `hives` grid in APL as KRON
- `lineage` tree (queen lines) in MUMPS as MYRA
