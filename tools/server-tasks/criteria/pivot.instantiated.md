# Criteria (instantiated) — pivot

matrix 136 · wave spec `wave11/136-pivot.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| limits (dwell 200..5000 ms, scan step 300..10000 ms, target  | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| profile record (switches, dwell, scan order) in myra | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- dwell select (every gaze sample inside the target for at least the dwell time) in eve + kron witnessed
- A dwell never confirms a held action (pay, grant, trash)
- those need a second dwell on a confirm target
