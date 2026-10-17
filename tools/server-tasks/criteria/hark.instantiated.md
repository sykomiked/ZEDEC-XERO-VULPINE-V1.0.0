# Criteria (instantiated) — hark

matrix 134 · wave spec `wave11/134-hark.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| limits (threshold 50..100, confirm window 3..30 s) in eve | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## Clauses the pattern library does not yet cover (author these)

- "may run by voice" = known command AND confidence at least the threshold AND not a held kind (trash, empty Trash, send, pay, grant, approve) without a spoken confirm, in eve + kron witnessed
- command phrase table (phrase, command, kind) in myra
- Voice never approves money, grants, deletions or access by itself
- those need the person's second confirm
