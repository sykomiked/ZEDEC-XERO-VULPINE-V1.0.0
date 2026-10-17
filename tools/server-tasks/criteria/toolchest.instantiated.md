# Criteria (instantiated) — toolchest

matrix 126 · wave spec `wave1/126-toolchest.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "may install" = both signatures valid AND fits AND size at m | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| kit record (name, version, language, size) in myra | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- kit fits this zxvd (major.minor.patch compare) in sutra + kron witnessed
- download and installed sizes exact in trebago
- Never install anything in tests
