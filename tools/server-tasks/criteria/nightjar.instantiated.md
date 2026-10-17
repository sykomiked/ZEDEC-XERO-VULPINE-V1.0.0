# Criteria (instantiated) — nightjar

matrix 133 · wave spec `wave4/133-nightjar.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| speech limits (80..450 words a minute, pitch 50..200) in eve | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| element record (role, name, row, column, state) in myra | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| element record (role, name, row, column, state) in myra | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- "interrupt now" (alerts and focus changes cut in, page reading does not) in eve + kron witnessed
- reading order (by row, then column) in kron
