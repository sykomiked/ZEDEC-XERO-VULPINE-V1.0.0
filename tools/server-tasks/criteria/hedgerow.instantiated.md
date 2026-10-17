# Criteria (instantiated) — hedgerow

matrix 30 · wave spec `wave2/030-hedgerow.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| rule record in myra | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- "is this path reachable" (mode, switch, private bind) in eve + kron + C++ wasm (three witnesses)
- address class (loopback, private, link-local, unique-local, from octets) in Rust wasm + kron witnessed
- Opening anything is asked first
- closing is at once
