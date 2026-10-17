# Criteria (instantiated) — plinth

matrix 254 · wave spec `wave21/254-plinth.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| the sum within the disc) in FORTRAN as Trebago + APL as KRON | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| - install transaction log (steps with byte counts, refusing  | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - partition table records in MUMPS as MYRA | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| the test, on an image file, asserts state 3 and that the dis | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - (critical) "may write this disc" = blank by both probes AND the owner typed this disc's id to confirm AND it is not the disc the system runs from, in Ada as Eve + APL as KRON witnessed
- - (critical) partition sizes (ESP, system A, system B, models, encrypted data
- 1 MiB aligned
- - block maps (partitions x start, size) in APL as KRON
- Paraconsistent (BOTH test, in test_zxapp.c): probe A finds no partition table (blank) and probe B finds a filesystem signature, giving BOTH
- never re-derive the tables
