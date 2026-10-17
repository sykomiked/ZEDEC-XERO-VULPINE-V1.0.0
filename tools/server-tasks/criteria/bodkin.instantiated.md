# Criteria (instantiated) — bodkin

matrix 183 · wave spec `wave15/183-bodkin.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| piece size (measurement + ease, graded between sizes, millim | `grade_band` | num | `value,b1,b2,b3,b4` | kron, trebago | the band index 0..4 of the value across the four ascending bounds | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| order book (order number, price x quantity in minor units, r | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## Clauses the pattern library does not yet cover (author these)

- "measurements plausible" (each within its declared range) in Ada as Eve + APL as KRON witnessed
- measurement grid in APL as KRON
- garment tree in MUMPS as MYRA
