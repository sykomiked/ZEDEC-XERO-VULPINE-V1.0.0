# Criteria (instantiated) — meander

matrix 264 · wave spec `wave21/264-meander.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - document records and the script tree in MUMPS as MYRA | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| - Sutra is left out: no money or counted business records he | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| - Sutra is left out: no money or counted business records he | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| the test asserts state 3 and that both readings stay until c | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - (critical) "commit this reading" (best match's score clears the threshold and the next is at least the margin below) in Ada as Eve + APL as KRON witnessed
- - (critical) strokes resampled to n points by exact integer arc length in FORTRAN as Trebago + APL as KRON witnessed
- - glyph matrix (stroke features x templates, distances) in APL as KRON
- Paraconsistent (BOTH test, in test_zxapp.c): a stroke that reads as "cl" and as "d" within the margin gives BOTH
- never re-derive the tables
