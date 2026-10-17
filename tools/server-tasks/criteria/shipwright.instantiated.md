# Criteria (instantiated) — shipwright

matrix 190 · wave spec `wave15/190-shipwright.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| water level ahead (gauge trend plus rainfall in mm x the cat | `grade_band` | num | `value,b1,b2,b3,b4` | kron, trebago | the band index 0..4 of the value across the four ascending bounds | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| "warn this district" (level at or above the threshold, or a  | `grade_band` | num | `value,b1,b2,b3,b4` | kron, trebago | the band index 0..4 of the value across the four ascending bounds | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |

## Clauses the pattern library does not yet cover (author these)

- alert thresholds in Ada as Eve
- gauge grid in APL as KRON
- district tree in MUMPS as MYRA
