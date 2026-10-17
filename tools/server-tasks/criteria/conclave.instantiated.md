# Criteria (instantiated) — conclave

matrix 369 · wave spec `wave31/369-conclave.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `budget`: (critical) token budget per agent per cycle over | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `budget`: (critical) token budget per agent per cycle over | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| - `budget_ok`: budget limits (no agent above its cap, the sw | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `records`: agent records in MUMPS as MYRA | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| the test asserts state 3 and that both answers and their evi | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `accept_ok`: (critical) "act on this suggestion" (the person accepted it AND Sieve's `may this be applied` TRUE AND the agents agree or the person chose a side) in Ada as Eve + APL as KRON witnessed
- nothing acts on BOTH
- - `agents`: agent tensor tables (agents x tasks x tokens) in APL as KRON
- - `cycles`: cycle ledger (cycles run and tokens spent, refusing on overflow) in COBOL as Sutra
- Paraconsistent (BOTH test, in test_zxapp.c): two agents give different answers with evidence: the result is BOTH
- never re-derive the tables
