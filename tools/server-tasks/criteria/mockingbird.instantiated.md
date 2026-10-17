# Criteria (instantiated) — mockingbird

matrix 128 · wave spec `wave3/128-mockingbird.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| cycle count per run, refused on overflow, in sutra + trebago | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| limits (RAM at most 4 GiB, at most 64 devices, at most 10^9  | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| limits (RAM at most 4 GiB, at most 64 devices, at most 10^9  | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| device record (kind, base, size, irq) in myra | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- boot self-check passed (every stage's code 0 in order) in eve + kron witnessed
- memory map regions do not overlap (sorted base and size vectors) in kron + trebago witnessed
