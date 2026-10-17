# Criteria (instantiated) — flue

matrix 189 · wave spec `wave15/189-flue.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "sync fits" (bytes at most the target's free bytes and quota | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| seconds to send (bytes / rate) exact in FORTRAN as Trebago | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |

## Clauses the pattern library does not yet cover (author these)

- version vector compare (dominates, equal or concurrent) in APL as KRON + FORTRAN as Trebago witnessed
- sync ledger (per file, per device, versions strictly rising, refused on reuse or overflow) in COBOL as Sutra
- folder tree in MUMPS as MYRA
