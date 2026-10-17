# Criteria (instantiated) — isthmus

matrix 240 · wave spec `wave19/240-isthmus.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - (critical) settlement split of an amount by agreed proport | `share_x100` | num | `part,whole` | kron, trebago | the part's share of the whole in hundredths of a percent, floor: part  | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| - (critical) settlement split of an amount by agreed proport | `largest_remainder` | num | `pool,w1,w2,w3,w4` | sutra, trebago | the first recipient's share of the pool by weight (floor), plus one wh | `check largest_remainder pool=100,10,7,100 w1=1,1,1,1 w2=1,1,1,1 w3=1,1,1,1 w4=1,0,0,0 = 25,4,3,34` |
| - case records (fees and amounts in minor units, refusing on | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| - case records (fees and amounts in minor units, refusing on | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| the test asserts state 3, neither account marked false, and  | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| the test asserts state 3, neither account marked false, and  | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - (critical) "next step allowed" (both parties heard, cooling-off over, both consented to the proposal) in Ada as Eve + APL as KRON witnessed
- - claim matrix (claims x parties: agreed, disputed) in APL as KRON
- - party tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): party A says the goods were delivered on 3 May and party B says they were not, giving BOTH
- never re-derive the tables
