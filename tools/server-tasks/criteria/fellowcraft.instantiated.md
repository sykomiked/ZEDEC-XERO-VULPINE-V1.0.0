# Criteria (instantiated) — fellowcraft

matrix 207 · wave spec `wave17/207-fellowcraft.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| say so in the manifest), and the counts per value are in APL | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| say so in the manifest), and the counts per value are in APL | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| elements that differ and the largest difference exact in FOR | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| contract checks (both runs inside their fuel and output limi | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## Clauses the pattern library does not yet cover (author these)

- the overall verdict joins each element's result with `lpres_conjoin` (C, kernel order, never re-derived
- comparison ledger (comparisons, agreements and disagreements, refused on overflow) in COBOL as Sutra
- run tree in MUMPS as MYRA
