# Criteria (instantiated) — cupel

matrix 179 · wave spec `wave14/179-cupel.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| precision bounds (fixed-point scale, at most the declared st | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| precision bounds (fixed-point scale, at most the declared st | `clamp` | num | `value,min,max` | kron, trebago | value clamped into [min,max] | `check clamp value=-5,0,50,150 min=0,0,0,0 max=100,100,100,100 = 0,0,50,100` |
| model tree and run records in MUMPS as MYRA | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- one integer leapfrog step (fixed-point positions and velocities, forces from a pair table) in FORTRAN as Trebago + APL as KRON witnessed
- lattice arrays in APL as KRON
