# Criteria (instantiated) — watchfire

matrix 143 · wave spec `wave11/143-watchfire.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "share location with this contact" = person started the emer | `share_x100` | num | `part,whole` | kron, trebago | the part's share of the whole in hundredths of a percent, floor: part  | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| medical ID and contact records in myra, stored as the person | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- cancel window before any ring (10..60 s, never skipped) in eve + kron witnessed
- Life-safety: advisory and human-approved
- never calls emergency services or anyone by itself
- not a medical device
- nothing clinical is inferred
