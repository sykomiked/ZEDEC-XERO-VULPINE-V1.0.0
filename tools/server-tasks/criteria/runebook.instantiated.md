# Criteria (instantiated) — runebook

matrix 114 · wave spec `wave10/114-runebook.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| block of a code point (search over the block-start vector) a | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## Clauses the pattern library does not yet cover (author these)

- valid code point (0..0x10FFFF, not a surrogate, not a noncharacter) in kron + trebago witnessed
- UTF-8 length and bytes of a code point in trebago + kron witnessed
