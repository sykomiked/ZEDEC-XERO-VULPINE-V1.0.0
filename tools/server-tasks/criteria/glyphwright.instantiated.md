# Criteria (instantiated) — glyphwright

matrix 137 · wave spec `wave11/137-glyphwright.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| composed character (dead key + base from the table | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| layout record (layout, key, level, code point) in myra | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| layout record (layout, key, level, code point) in myra | `grade_band` | num | `value,b1,b2,b3,b4` | kron, trebago | the band index 0..4 of the value across the four ascending bounds | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |

## Clauses the pattern library does not yet cover (author these)

- Hangul L, V, T by 0xAC00 + (L x 21 + V) x 28 + T) in myra + kron witnessed
- result is a valid code point (0..0x10FFFF, not a surrogate) in eve
