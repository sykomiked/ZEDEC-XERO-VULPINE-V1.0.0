# Criteria (instantiated) — druse

matrix 324 · wave spec `wave27/324-druse.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `media_decay` (years left per medium from its declared life  | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `storage_fees` (in minor units) in COBOL as Sutra | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## Clauses the pattern library does not yet cover (author these)

- `access_ok` (opening a cold copy needs Moraine's release AND the owner or guardians present) in Ada as Eve + APL as KRON witnessed
- `shelf` grid in APL as KRON
- `holdings` tree in MUMPS as MYRA
