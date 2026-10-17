# meander — the complete brief

| | |
|---|---|
| matrix layer | 264 |
| icon group | L2 |
| wave spec | `wave21/264-meander.md` |

## Purpose

Writes by hand or brush and turns strokes into text in any script: documents in the Hoard (`files.read`, `files.write`), the script's language (`xlate.detect`, `xlate.languages`). Pen input and stroke recognition have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `ai.ask`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | document records | primary | runs |
| FORTRAN | `trebago` | numeric | stroke curves | primary | runs |
| Ada | `eve` | limits | recognition limits | primary | runs |
| APL | `kron` | arrays | glyph matrix | primary | runs |
| MUMPS | `myra` | records | script tree | primary | runs |
| Perl | `-` | text | text | primary | planned: needs Lingua front-end |
| Tcl | `-` | scripting | scripting | primary | planned: needs Lingua front-end |
| OCaml | `-` | parse | parsing | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Perl): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) "commit this reading" (best match's score clears the threshold and the next is at least the margin below) in Ada as Eve + APL as KRON witnessed
- - (critical) strokes resampled to n points by exact integer arc length in FORTRAN as Trebago + APL as KRON witnessed
- - glyph matrix (stroke features x templates, distances) in APL as KRON
- - document records and the script tree in MUMPS as MYRA
- - Sutra is left out: no money or counted business records here
- Paraconsistent (BOTH test, in test_zxapp.c): a stroke that reads as "cl" and as "d" within the margin gives BOTH
- the test asserts state 3 and that both readings stay until context or the writer settles it. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id meander
matrix 264
name Meander
needs fs.read
needs fs.write
needs ai.ask
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

