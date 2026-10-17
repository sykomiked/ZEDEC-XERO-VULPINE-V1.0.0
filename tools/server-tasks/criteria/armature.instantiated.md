# Criteria (instantiated) — armature

matrix 297 · wave spec `wave25/297-armature.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `test_calc` (derived results from measurements, reduced rati | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `prototype_costs` (in minor units) in COBOL as Sutra + FORTR | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## Clauses the pattern library does not yet cover (author these)

- `proof_ok` (an entry is dated proof only when its content id is on the journal and the chain is intact) in Ada as Eve + APL as KRON witnessed
- `results` matrix in APL as KRON
- `revisions` tree in MUMPS as MYRA
