# Criteria (instantiated) — gauntlet

matrix 129 · wave spec `wave10/129-gauntlet.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| counts add up (killed + survived + equivalent = total, refus | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| gate (score at least the target's floor, at most 10000 mutan | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| survivor record (file, line, operator) in myra | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- mutation score x100 (killed / (total - equivalent)) in kron + trebago witnessed
- Mutants never touch committed files
- they run on copies
