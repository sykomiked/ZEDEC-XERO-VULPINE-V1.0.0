# Criteria (instantiated) — portcullis

matrix 336 · wave spec `wave28/336-portcullis.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `admit` (ticket valid AND on the list AND venue under capaci | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `ticket_code` (ticket code well formed, bounded bytes and ch | `clamp` | num | `value,min,max` | kron, trebago | value clamped into [min,max] | `check clamp value=-5,0,50,150 min=0,0,0,0 max=100,100,100,100 = 0,0,50,100` |
| `ticket_sales` (in minor units) in COBOL as Sutra + FORTRAN  | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## Clauses the pattern library does not yet cover (author these)

- `crowd_flow` (people per minute through each gate) in FORTRAN as Trebago + APL as KRON witnessed
- `entry` grid in APL as KRON
- `guests` hierarchy in MUMPS as MYRA
