# Criteria (instantiated) — joinery

matrix 121 · wave spec `wave3/121-joinery.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| buffer limits (file at most 16 MiB, line at most 65536 bytes | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| buffer limits (file at most 16 MiB, line at most 65536 bytes | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| undo record (step, offset, removed length, inserted length)  | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| undo record (step, offset, removed length, inserted length)  | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |
| it is left out. Parsing source text stays C (its best-fit la | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |

## Clauses the pattern library does not yet cover (author these)

- line and column to byte offset and back (UTF-8 widths as integer vectors) in kron + trebago witnessed
- COBOL owns no decision in an editor
- say so in the manifest
