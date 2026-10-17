# Criteria (instantiated) — belvedere

matrix 87 · wave spec `wave8/087-belvedere.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| page byte totals exact in trebago | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |

## Clauses the pattern library does not yet cover (author these)

- "may this file go on the cloud top" (a copy into a more open kind needs the owner's confirmation) in eve + myra witnessed
- published items and their CIDs in myra (CIDs come from the daemon only)
- Nothing is published without the owner's confirmation
