# Criteria (instantiated) — loupe

matrix 181 · wave spec `wave15/181-loupe.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "evidence admissible" (content id unchanged since it was reg | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| timeline gaps (differences between event times in ms, the la | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| event matrix (source by minute counts) in APL as KRON | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## Clauses the pattern library does not yet cover (author these)

- evidence register (item number, custody hand-offs, never reused, refused on overflow) in COBOL as Sutra
- case tree in MUMPS as MYRA
