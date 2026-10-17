# Criteria (instantiated) — equant

matrix 320 · wave spec `wave27/320-equant.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `damages` (proven losses plus interest at the stated rate, r | `conserved_sum` | truth | `whole,a,b,c,d` | trebago, sutra | true when a + b + c + d == whole exactly (nothing created or destroyed | `check conserved_sum whole=100,100,100,101 a=25,0,0,25 b=25,0,0,25 c=25,0,0,25 d=25,100,99,25 = true,true,false,false` |
| `damages` (proven losses plus interest at the stated rate, r | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `settlement` (agreed amounts in minor units) in COBOL as Sut | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## Clauses the pattern library does not yet cover (author these)

- `procedure_ok` (each step inside its deadline under the published rulebook) in Ada as Eve + APL as KRON witnessed
- `evidence` matrix in APL as KRON
- `cases` hierarchy in MUMPS as MYRA
