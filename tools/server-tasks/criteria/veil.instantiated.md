# Criteria (instantiated) — veil

matrix 365 · wave spec `wave30/365-veil.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `disclose_ok`: (critical) "disclose" (allowed by the perso | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| - `anonymity`: (critical) anonymity metric (group size k for | `share_x100` | num | `part,whole` | kron, trebago | the part's share of the whole in hundredths of a percent, floor: part  | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| - `anonymity`: (critical) anonymity metric (group size k for | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| - `fees`: data-access fees asked of outside requesters (mino | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| the test asserts state 3 and that the data is withheld until | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `access`: access grid (apps and people x data kinds) in APL as KRON
- - `identity`: identity tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): a request is permitted by one rule and forbidden by another: it is BOTH
- never re-derive the tables
