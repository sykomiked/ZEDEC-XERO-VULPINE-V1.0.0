# Criteria (instantiated) — pannier

matrix 150 · wave spec `wave12/150-pannier.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "send now" = peer active AND parcel within size AND attempt  | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| "send now" = peer active AND parcel within size AND attempt  | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| retry limits (at most 16 attempts, back-off capped at 3600 s | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| retry limits (at most 16 attempts, back-off capped at 3600 s | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| route timing (expected hours as the sum of hop times) exact  | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| the queue as a bounded ring (head - tail >= capacity is back | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| the queue as a bounded ring (head - tail >= capacity is back | `clamp` | num | `value,min,max` | kron, trebago | value clamped into [min,max] | `check clamp value=-5,0,50,150 min=0,0,0,0 max=100,100,100,100 = 0,0,50,100` |

## Clauses the pattern library does not yet cover (author these)

- delivery receipts (sequence per peer strictly rising, refused on reuse or overflow) in COBOL as Sutra + APL as KRON witnessed
- parcel tree in MUMPS as MYRA
