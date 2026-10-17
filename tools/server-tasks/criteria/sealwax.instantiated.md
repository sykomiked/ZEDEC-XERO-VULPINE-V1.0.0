# Criteria (instantiated) — sealwax

matrix 47 · wave spec `wave6/047-sealwax.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "ready to sign" (every required field filled, each within it | `clamp` | num | `value,min,max` | kron, trebago | value clamped into [min,max] | `check clamp value=-5,0,50,150 min=0,0,0,0 max=100,100,100,100 = 0,0,50,100` |
| amount fields (fixed scale, refused on overflow) in sutra | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| field record in myra | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- guided fill order in basic
- Signing is the person's own act: the app never signs or submits by itself
- legal and financial forms are advisory
