# Criteria (instantiated) — cartulary

matrix 145 · wave spec `wave12/145-cartulary.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| retention schedule rows (class code, years, months) as fixed | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| retention limits (1 day to 36525 days, a hold always wins) i | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| records per class per year in APL as KRON (the index grid) | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| record hierarchy (class, record, version, filed day, hold) i | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- "may this version be pruned" = schedule over AND no hold AND `keepsake.keep_ok` in Ada as Eve + APL as KRON witnessed
- storage growth (bytes per month as a vector, projected bytes after n months from the mean monthly growth, floor) exact in FORTRAN as Trebago
