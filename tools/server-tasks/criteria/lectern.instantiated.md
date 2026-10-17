# Criteria (instantiated) — lectern

matrix 151 · wave spec `wave12/151-lectern.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| citation register (citation number, source id, page) as fixe | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| source limits (at most 256 sources a document) in Ada as Eve | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| source limits (at most 256 sources a document) in Ada as Eve | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| concept matrix (concept by source counts) in APL as KRON | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## Clauses the pattern library does not yet cover (author these)

- "make a card from this highlight" (offset + length inside the document, length 1..4096 bytes) in Ada as Eve + APL as KRON witnessed
- reading minutes per document per week exact in FORTRAN as Trebago
- chapter tree in MUMPS as MYRA
