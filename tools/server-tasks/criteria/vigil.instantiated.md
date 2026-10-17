# Criteria (instantiated) — vigil

matrix 274 · wave spec `wave23/274-vigil.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `incident_bill` (minutes paged per incident x the declared r | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |

## Clauses the pattern library does not yet cover (author these)

- `page_now` (Headland's alarm TRUE AND not a soft watch AND the on-call person's shift now) in Ada as Eve + APL as KRON witnessed
- `load_forecast` (next hour's load from the integer trend) in FORTRAN as Trebago + APL as KRON witnessed
- `metrics` arrays in APL as KRON
- `systems` hierarchy in MUMPS as MYRA
