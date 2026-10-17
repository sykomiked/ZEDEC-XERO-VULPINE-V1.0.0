# Criteria (instantiated) — ramble

matrix 25 · wave spec `wave2/025-ramble.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "consent covers this" (scope matches, amount_minor within ce | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| "consent covers this" (scope matches, amount_minor within ce | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| envelope index in myra | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- CID string check (prefix, length, base32 alphabet, as byte codes) in Rust wasm + kron witnessed
- Ramble never grants consent by itself: a grant with money is advisory and the owner's act
