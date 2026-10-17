# Criteria (instantiated) — archivolt

matrix 322 · wave spec `wave27/322-archivolt.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `record_map` (old fixed records mapped field by field into C | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `record_map` (old fixed records mapped field by field into C | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- `cutover_ok` (every parity check TRUE over the declared run window, owner approved) in Ada as Eve + APL as KRON witnessed
- `parity` (old and new numeric results equal, element by element) in FORTRAN as Trebago + COBOL as Sutra witnessed
- `fields` mapping grid in APL as KRON
- `legacy` hierarchy in MUMPS as MYRA
