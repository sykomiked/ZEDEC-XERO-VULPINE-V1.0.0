# Criteria (instantiated) — rotunda

matrix 331 · wave spec `wave27/331-rotunda.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `usage_charges` (hours x rate shared by group, largest remai | `share_x100` | num | `part,whole` | kron, trebago | the part's share of the whole in hundredths of a percent, floor: part  | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `usage_charges` (hours x rate shared by group, largest remai | `largest_remainder` | num | `pool,w1,w2,w3,w4` | sutra, trebago | the first recipient's share of the pool by weight (floor), plus one wh | `check largest_remainder pool=100,10,7,100 w1=1,1,1,1 w2=1,1,1,1 w3=1,1,1,1 w4=1,0,0,0 = 25,4,3,34` |

## Clauses the pattern library does not yet cover (author these)

- `operator_ok` (a booking only for someone certified on that instrument and not expired) in Ada as Eve + APL as KRON witnessed
- `slots` (fit requests into free slots, earliest first, no overlap) in FORTRAN as Trebago + APL as KRON witnessed
- `bookings` grid in APL as KRON
- `facilities` tree in MUMPS as MYRA
