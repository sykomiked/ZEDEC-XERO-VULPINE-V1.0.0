# Criteria (instantiated) — talewind

matrix 108 · wave spec `wave9/108-talewind.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "choice shown" (required flags held, age rating allowed) in  | `flags_none` | truth | `bits,forbidden` | eve | true when (bits & forbidden) == 0 (no refused bit set) | `check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false` |
| story graph (passages, choices, flags) in myra | `flags_none` | truth | `bits,forbidden` | eve | true when (bits & forbidden) == 0 (no refused bit set) | `check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- AI-drafted passages are held for the author's approval
