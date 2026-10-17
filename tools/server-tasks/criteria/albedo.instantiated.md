# Criteria (instantiated) — albedo

matrix 276 · wave spec `wave23/276-albedo.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `claim_state` (each credible source's verdict on a claim joi | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |
| KRON counts sources per value) with `publish_ok` (a verdict  | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| KRON counts sources per value) with `publish_ok` (a verdict  | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `credibility` (a source's record of confirmed over total cla | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `credibility` (a source's record of confirmed over total cla | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |

## Clauses the pattern library does not yet cover (author these)

- `subscriptions` (source subscriptions in minor units) in COBOL as Sutra
- `claims` matrix in APL as KRON
- `citations` tree in MUMPS as MYRA
