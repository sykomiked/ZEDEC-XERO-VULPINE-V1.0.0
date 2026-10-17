# Criteria (instantiated) — lunette

matrix 319 · wave spec `wave26/319-lunette.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `guest_capacity` (guests at most the venue's seats, with the | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `guest_capacity` (guests at most the venue's seats, with the | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `fees` (ceremony fees in minor units) in COBOL as Sutra + FO | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## Clauses the pattern library does not yet cover (author these)

- `notice_ok` (the declared legal notice period between filing and the ceremony day is met) in Ada as Eve + APL as KRON witnessed
- `venues` calendar grid in APL as KRON
- `family` tree in MUMPS as MYRA
