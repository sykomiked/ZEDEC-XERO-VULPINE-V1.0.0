# Criteria (instantiated) — skep

matrix 185 · wave spec `wave15/185-skep.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| grant ledger (each contributor's grant by weight, largest re | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| grant ledger (each contributor's grant by weight, largest re | `largest_remainder` | num | `pool,w1,w2,w3,w4` | sutra, trebago | the first recipient's share of the pool by weight (floor), plus one wh | `check largest_remainder pool=100,10,7,100 w1=1,1,1,1 w2=1,1,1,1 w3=1,1,1,1 w4=1,0,0,0 = 25,4,3,34` |
| task placement (tasks to nodes by spare capacity, largest re | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| task placement (tasks to nodes by spare capacity, largest re | `largest_remainder` | num | `pool,w1,w2,w3,w4` | sutra, trebago | the first recipient's share of the pool by weight (floor), plus one wh | `check largest_remainder pool=100,10,7,100 w1=1,1,1,1 w2=1,1,1,1 w3=1,1,1,1 w4=1,0,0,0 = 25,4,3,34` |
| "within the agent's budget" in Ada as Eve + APL as KRON witn | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## Clauses the pattern library does not yet cover (author these)

- task matrix in APL as KRON
- colony tree in MUMPS as MYRA
