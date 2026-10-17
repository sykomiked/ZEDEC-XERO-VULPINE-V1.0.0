# rill — the complete brief

| | |
|---|---|
| matrix layer | 216 |
| icon group | L2 |
| wave spec | `wave17/216-rill.md` |

## Purpose

gives children a safe reading space with read-aloud, word help and story choices, from the Hoard (`files.read`) within their screen time (`screen.check`, `screen.status`). zxvd has no method for text-to-speech: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `screen.time`
- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | reading log | primary | runs |
| FORTRAN | `trebago` | numeric | reading-level model | primary | runs |
| Ada | `eve` | limits | age limits | primary | runs |
| APL | `kron` | arrays | vocabulary grid | primary | runs |
| MUMPS | `myra` | records | child profile tree | primary | runs |
| BASIC | `basic` | stepwise | step-by-step teaching | primary | runs |
| Mojo | `-` | ai | AI | primary | planned: needs Lingua front-end |
| AWK | `-` | text | text | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **basic** (BASIC): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (Mojo): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "age-appropriate" (the book's band at most the child's) in Ada as Eve + APL as KRON witnessed
- reading level (words per sentence and syllables per word, integer formula) in FORTRAN as Trebago + APL as KRON witnessed
- word help steps (sound it out, meaning, example) in BASIC (twin Hatchling)
- vocabulary grid (child by word) in APL as KRON
- child profile tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `grade_band` | num | `value b1 b2 b3 b4` | kron | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |

## The manifest contract

```
zxapp 1
id rill
matrix 216
name Rill
needs fs.read
needs screen.time
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

