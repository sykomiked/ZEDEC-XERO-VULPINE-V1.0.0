# Criteria (instantiated) — bonfire

matrix 95 · wave spec `wave8/095-bonfire.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "may this guest join" (on the invite list, room left) in eve | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| seats left and RSVP counts exact in trebago | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| seats left and RSVP counts exact in trebago | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## Clauses the pattern library does not yet cover (author these)

- events and invite lists in myra
