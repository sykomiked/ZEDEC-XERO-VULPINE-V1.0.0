# Criteria (instantiated) — heirloom

matrix 81 · wave spec `wave8/081-heirloom.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| manifest fractions sum to exactly 1 (cross-multiplied) in su | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| manifest fractions sum to exactly 1 (cross-multiplied) in su | `share_x100` | num | `part,whole` | kron, trebago | the part's share of the whole in hundredths of a percent, floor: part  | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| split preview by largest remainder in sutra + trebago witnes | `share_x100` | num | `part,whole` | kron, trebago | the part's share of the whole in hundredths of a percent, floor: part  | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| split preview by largest remainder in sutra + trebago witnes | `largest_remainder` | num | `pool,w1,w2,w3,w4` | sutra, trebago | the first recipient's share of the pool by weight (floor), plus one wh | `check largest_remainder pool=100,10,7,100 w1=1,1,1,1 w2=1,1,1,1 w3=1,1,1,1 w4=1,0,0,0 = 25,4,3,34` |
| owner and guardians sign. Tests make their own throwaway key | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- "release due" (k guardian approvals and the waiting period over, no living veto) in eve + trebago witnessed
- Nothing happens on time or silence alone
