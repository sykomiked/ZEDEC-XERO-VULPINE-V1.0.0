# Criteria (instantiated) — broadsheet

matrix 42 · wave spec `wave6/042-broadsheet.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| annotation record in myra | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- "well formed" (header present, every cross-reference offset inside the file size) in Rust wasm + eve witnessed, so a malformed file is refused
- zoom to fit page in kron
