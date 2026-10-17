# Criteria (instantiated) — relume

matrix 273 · wave spec `wave23/273-relume.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `recovery_min` (minutes to restore = bytes / measured rate,  | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `recovery_min` (minutes to restore = bytes / measured rate,  | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `records` hierarchy in MUMPS as MYRA | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- `restore_next` (the next item to restore: every item it depends on already restored and verified) in Ada as Eve + APL as KRON witnessed
- `restored_books` (each restored ledger's balance equals the backup's, minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `service_deps` (service dependency matrix) in APL as KRON
