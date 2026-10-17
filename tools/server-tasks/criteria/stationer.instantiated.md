# Criteria (instantiated) — stationer

matrix 202 · wave spec `wave16/202-stationer.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| form registry (form number, version, never reused) in COBOL  | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## Clauses the pattern library does not yet cover (author these)

- field boxes fit the page (twips) in FORTRAN as Trebago + APL as KRON witnessed
- "field valid" (length, range and required as declared) in Ada as Eve + APL as KRON witnessed
- field grid in APL as KRON
- form tree in MUMPS as MYRA
