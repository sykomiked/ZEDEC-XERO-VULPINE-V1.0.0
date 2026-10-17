# Criteria (instantiated) — hornbook

matrix 169 · wave spec `wave14/169-hornbook.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "age-appropriate" (the lesson's band at most the learner's)  | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| "age-appropriate" (the lesson's band at most the learner's)  | `grade_band` | num | `value,b1,b2,b3,b4` | kron, trebago | the band index 0..4 of the value across the four ascending bounds | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| enrolment records (learner, class, seat | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- next lesson step from the learner's answer in BASIC (twin Hatchling) + APL as KRON witnessed
- seats per class refused on overflow) in COBOL as Sutra
- score statistics (mean floor, min, max per question) exact in FORTRAN as Trebago
- mastery grid (learner by skill, mastered after k right in a row) in APL as KRON
- learner tree in MUMPS as MYRA
