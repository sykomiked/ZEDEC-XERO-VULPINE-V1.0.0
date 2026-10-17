# Criteria (instantiated) — commons

matrix 27 · wave spec `wave2/027-commons.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| text limits (1..1024 bytes, reaction 1..32) in eve | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| unread counts per room in kron + trebago | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| room and message index in myra | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| Feeds keep the daemon's ranking | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## Clauses the pattern library does not yet cover (author these)

- "may I post here" (member, room joined, switch on) in eve + kron witnessed
- nothing is re-ranked by likes or views
