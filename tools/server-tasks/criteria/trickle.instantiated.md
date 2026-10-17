# Criteria (instantiated) — trickle

matrix 77 · wave spec `wave8/077-trickle.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| amount due at tick t = floor(total x t / T) minus already pa | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| amount due at tick t = floor(total x t / T) minus already pa | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| "stream allowed" (under the owner's per-day cap and balance) | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## Clauses the pattern library does not yet cover (author these)

- a tithe = floor(income x num / den) with the carried remainder in sutra + trebago witnessed
- A stream starts, changes or stops only on the owner's approval
- no unit is lost to rounding (remainders carry)
