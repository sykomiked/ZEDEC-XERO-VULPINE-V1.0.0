# Criteria (instantiated) — equinox

matrix 312 · wave spec `wave26/312-equinox.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `scheduled_payments` (amounts in minor units, released only  | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## Clauses the pattern library does not yet cover (author these)

- `release_date_ok` (today at or after the release day AND Heirloom's release due for a will) in Ada as Eve + APL as KRON witnessed
- `days_until` (Gregorian day arithmetic) in FORTRAN as Trebago + APL as KRON witnessed
- `calendar` grid in APL as KRON
- `recipients` tree in MUMPS as MYRA
