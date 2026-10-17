# Criteria (instantiated) — chainman

matrix 154 · wave spec `wave12/154-chainman.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "closes within tolerance" (closure error at most the declare | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| title register (title number, parcel, area) as fixed records | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- parcel area (twice the area by the shoelace rule over corner vectors in millimetres, exact) in FORTRAN as Trebago + APL as KRON witnessed
- parcel grid (cells claimed by each parcel, overlaps found) in APL as KRON
- land hierarchy in MUMPS as MYRA
