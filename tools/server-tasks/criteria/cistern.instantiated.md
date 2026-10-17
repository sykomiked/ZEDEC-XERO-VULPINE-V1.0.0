# Criteria (instantiated) — cistern

matrix 296 · wave spec `wave25/296-cistern.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `pressure_dose_ok` (pressure inside the declared band AND di | `grade_band` | num | `value,b1,b2,b3,b4` | kron, trebago | the band index 0..4 of the value across the four ascending bounds | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| `billing` (cubic metres x tariff bands in minor units) in CO | `grade_band` | num | `value,b1,b2,b3,b4` | kron, trebago | the band index 0..4 of the value across the four ascending bounds | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| `network_assets` tree in MUMPS as MYRA | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |

## Clauses the pattern library does not yet cover (author these)

- `hydraulic` (flow and head loss per pipe from the integer tables) in FORTRAN as Trebago + APL as KRON witnessed
- `meters` arrays in APL as KRON
