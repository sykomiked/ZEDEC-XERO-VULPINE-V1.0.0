# Criteria (instantiated) — umbra

matrix 314 · wave spec `wave26/314-umbra.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `linkability` (shared attributes between two personas, weigh | `share_x100` | num | `part,whole` | kron, trebago | the part's share of the whole in hundredths of a percent, floor: part  | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `linkability` (shared attributes between two personas, weigh | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `persona_accounts` (books per persona kept apart, minor unit | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `permissions` grid (persona by capability) in APL as KRON | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `permissions` grid (persona by capability) in APL as KRON | `flags_none` | truth | `bits,forbidden` | eve | true when (bits & forbidden) == 0 (no refused bit set) | `check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- `disclose_ok` (a field crosses from one persona to another only with the person's say for that field) in Ada as Eve + APL as KRON witnessed
- `identity` tree in MUMPS as MYRA
