# Criteria (instantiated) — reckoner

matrix 83 · wave spec `wave2/083-reckoner.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| period totals per account in trebago + sutra witnessed | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| period totals per account in trebago + sutra witnessed | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| tax estimate over integer bands in sutra + trebago witnessed | `grade_band` | num | `value,b1,b2,b3,b4` | kron, trebago | the band index 0..4 of the value across the four ascending bounds | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |

## Clauses the pattern library does not yet cover (author these)

- "report allowed" (closed period, `ledger.check` ok) in eve. Books verification is shown as `ledger.check` returns it
- Tax figures are advisory, not tax advice
- a person files
