# Criteria (instantiated) — icehouse

matrix 194 · wave spec `wave16/194-icehouse.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "thawed copy trusted" (size and content id equal the recorde | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| compression ratio x100 (packed x 10000 / unpacked) in FORTRA | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |

## Clauses the pattern library does not yet cover (author these)

- archive catalogue (archive number, bytes, refused on overflow) in COBOL as Sutra
- block arrays in APL as KRON
- snapshot tree in MUMPS as MYRA
