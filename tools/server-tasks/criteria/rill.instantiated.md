# Criteria (instantiated) — rill

matrix 216 · wave spec `wave17/216-rill.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "age-appropriate" (the book's band at most the child's) in A | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| "age-appropriate" (the book's band at most the child's) in A | `grade_band` | num | `value,b1,b2,b3,b4` | kron, trebago | the band index 0..4 of the value across the four ascending bounds | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| reading level (words per sentence and syllables per word, in | `grade_band` | num | `value,b1,b2,b3,b4` | kron, trebago | the band index 0..4 of the value across the four ascending bounds | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |

## Clauses the pattern library does not yet cover (author these)

- word help steps (sound it out, meaning, example) in BASIC (twin Hatchling)
- vocabulary grid (child by word) in APL as KRON
- child profile tree in MUMPS as MYRA
