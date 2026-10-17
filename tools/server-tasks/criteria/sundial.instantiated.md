# Criteria (instantiated) — sundial

matrix 110 · wave spec `wave9/110-sundial.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| minutes until the next alarm (minutes of day, weekday mask)  | `flags_none` | truth | `bits,forbidden` | eve | true when (bits & forbidden) == 0 (no refused bit set) | `check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false` |
| limits (alarms at most 64, snooze 1..60 minutes, timer at mo | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| limits (alarms at most 64, snooze 1..60 minutes, timer at mo | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| zone offsets as a vector by zone index in kron | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- local time for a zone (UTC minutes + offset, wrapped to 0..1439) in kron + trebago witnessed
