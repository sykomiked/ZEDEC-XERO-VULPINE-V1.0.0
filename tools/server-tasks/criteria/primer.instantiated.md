# Criteria (instantiated) — primer

matrix 100 · wave spec `wave9/100-primer.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| each pillar's exercises in that pillar (sutra money, trebago | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| each pillar's exercises in that pillar (sutra money, trebago | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |

## Clauses the pattern library does not yet cover (author these)

- "lesson passed" (`play.run` verdict TRUE and the expected output) in eve + kron witnessed
- first steps track in basic
- progress x100 in kron
- results in myra
- `play.draft` text is untrusted: shown as a hint, never run without the learner's action
