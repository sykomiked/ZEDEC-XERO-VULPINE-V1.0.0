# Criteria (instantiated) — diadem

matrix 321 · wave spec `wave27/321-diadem.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `tally` (counts per option over ballot arrays) in APL as KRO | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `costs` (election costs in minor units) in COBOL as Sutra | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## Clauses the pattern library does not yet cover (author these)

- `eligible` (on the roll AND not yet voted AND inside the voting window) in Ada as Eve + APL as KRON witnessed
- `ballots` arrays in APL as KRON
- `voter_roll` tree in MUMPS as MYRA
