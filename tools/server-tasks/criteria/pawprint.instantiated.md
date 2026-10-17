# Criteria (instantiated) — pawprint

matrix 122 · wave spec `wave10/122-pawprint.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| steps and instructions counted exact in trebago | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| limits (at most 256 breakpoints, stack view at most 64 frame | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| limits (at most 256 breakpoints, stack view at most 64 frame | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| breakpoint record (file, line, condition, hits) in myra | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| it is left out | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |

## Clauses the pattern library does not yet cover (author these)

- "may attach" = own program AND owner approved AND not a system process, in eve + kron witnessed
- breakpoint hit (line in the sorted breakpoint vector) in kron + trebago witnessed
- COBOL owns no decision here
