# Criteria (instantiated) — shingle

matrix 213 · wave spec `wave17/213-shingle.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| every need known to `zxapp_cap_known` | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| budget declared) in Ada as Eve + APL as KRON witnessed, refu | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| registry ledger (admission number, slot, never reused) in CO | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| manifest records in MUMPS as MYRA | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- "admit" (slot in its layer, the same rule as `zxapp_slot_layer`: L1 1-144, L2 145-271, L3 272-407
- version ordering (major.minor.patch) in FORTRAN as Trebago + APL as KRON witnessed
- slot table of 407 in APL as KRON
