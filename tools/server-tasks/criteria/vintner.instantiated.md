# Criteria (instantiated) — vintner

matrix 91 · wave spec `wave1/091-vintner.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| sale value (bottles x price, minor units) in sutra + C++ was | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| volume through the stages (harvest, must, barrel, bottle, lo | `within_range` | truth | `value,min,max` | eve, kron | true when min <= value <= max | `check within_range value=0,50,100,101 min=0,0,0,0 max=100,100,100,100 = true,true,true,false` |
| volume through the stages (harvest, must, barrel, bottle, lo | `conserved_sum` | truth | `whole,a,b,c,d` | trebago, sutra | true when a + b + c + d == whole exactly (nothing created or destroyed | `check conserved_sum whole=100,100,100,101 a=25,0,0,25 b=25,0,0,25 c=25,0,0,25 d=25,100,99,25 = true,true,false,false` |
| cellar probe readings (raw counts to tenths of a degree, ran | `within_range` | truth | `value,min,max` | eve, kron | true when min <= value <= max | `check within_range value=0,50,100,101 min=0,0,0,0 max=100,100,100,100 = true,true,true,false` |
| cellar probe readings (raw counts to tenths of a degree, ran | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| Posting a sale needs the owner's confirmation | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## Clauses the pattern library does not yet cover (author these)

- lots and vintages in myra
