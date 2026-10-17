# Criteria (instantiated) — pavise

matrix 153 · wave spec `wave12/153-pavise.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "allow" = Gatehouse allows AND Hedgerow's path reachable AND | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| access ledger (decision number, app, capability) as fixed re | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| access ledger (decision number, app, capability) as fixed re | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| exposure score (open paths, keys expiring within 30 days, un | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| permission matrix (app by capability) in APL as KRON | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| permission matrix (app by capability) in APL as KRON | `flags_none` | truth | `bits,forbidden` | eve | true when (bits & forbidden) == 0 (no refused bit set) | `check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false` |
| declared policy bounds in Ada as Eve | `clamp` | num | `value,min,max` | kron, trebago | value clamped into [min,max] | `check clamp value=-5,0,50,150 min=0,0,0,0 max=100,100,100,100 = 0,0,50,100` |

## Clauses the pattern library does not yet cover (author these)

- trust tree in MUMPS as MYRA
