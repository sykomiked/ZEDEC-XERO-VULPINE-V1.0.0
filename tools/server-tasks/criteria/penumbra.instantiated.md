# Criteria (instantiated) — penumbra

matrix 284 · wave spec `wave24/284-penumbra.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `attack_path` (path score over the asset graph, summed weigh | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |

## Clauses the pattern library does not yet cover (author these)

- `in_scope` (target in the owner's declared scope list AND inside the test window AND not a system process) in Ada as Eve + APL as KRON witnessed
- `findings` (finding number, severity, never reused) in COBOL as Sutra
- `traffic` matrices in APL as KRON
- `assets` tree in MUMPS as MYRA
