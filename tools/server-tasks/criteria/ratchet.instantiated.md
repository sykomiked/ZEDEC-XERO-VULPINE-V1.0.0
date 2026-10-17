# Criteria (instantiated) — ratchet

matrix 152 · wave spec `wave12/152-ratchet.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "fault" (a reading outside its safe operating limits) in Ada | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| wear (hours run x load share, summed) and hours to the next  | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| wear (hours run x load share, summed) and hours to the next  | `share_x100` | num | `part,whole` | kron, trebago | the part's share of the whole in hundredths of a percent, floor: part  | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| parts and labour costs in minor units in COBOL as Sutra + FO | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## Clauses the pattern library does not yet cover (author these)

- next repair step from the check results in BASIC (twin Hatchling) + APL as KRON witnessed (the person sees one step at a time)
- sensor grid (sensor by hour) in APL as KRON
- machine parts tree in MUMPS as MYRA
