# Criteria (instantiated) — magpie

matrix 119 · wave spec `wave10/119-magpie.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| history ring (64 slots, head - tail >= capacity drops the ol | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| bytes held exact, at most 64 x 49152, in trebago | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| inputs are lengths and flags only | `flags_none` | truth | `bits,forbidden` | eve | true when (bits & forbidden) == 0 (no refused bit set) | `check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- "keep this copy" = not concealed (password field or marked private) AND within size AND app not excluded, in eve + kron witnessed
- Never log or store clipboard content in tests
