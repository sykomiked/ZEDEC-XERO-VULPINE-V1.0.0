# Criteria (instantiated) — homestead

matrix 94 · wave spec `wave8/094-homestead.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| energy totals per device exact in trebago | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |

## Clauses the pattern library does not yet cover (author these)

- "command allowed" (setpoint within the device's range, locks, heaters and ovens need the owner) in eve + forth witnessed
- devices and scenes in myra
- Locks, heat and cooking devices are human-approved
- AI suggestions are held, never run
