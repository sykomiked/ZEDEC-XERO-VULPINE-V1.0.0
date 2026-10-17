# theorbo — the complete brief

| | |
|---|---|
| matrix layer | 348 |
| icon group | L3 |
| wave spec | `wave29/348-theorbo.md` |

## Purpose

Composes, records and performs music with others: scores and stems in the Hoard (`files.read`, `files.write`), rehearsals in a hall (`hall.load`, `hall.play`, `hall.pause`, `hall.seek`, `hall.status`, `pspace.create`), royalty splits paid only on the owners' approval (`ledger.post`). Audio in and out has no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `mic`
- `peers`
- `net`
- `ledger.read`
- `ledger.post`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | royalty splits | primary | runs |
| FORTRAN | `trebago` | numeric | audio synthesis | primary | runs |
| Ada | `eve` | limits | loudness limits | primary | runs |
| APL | `kron` | arrays | score arrays | primary | runs |
| MUMPS | `myra` | records | song version tree | primary | runs |
| PostScript | `-` | layout | page layout | primary | planned: needs Lingua front-end |
| Simula I | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| LabVIEW | `-` | signal | signals | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (PostScript): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `split_ok`: (critical) "royalty split agreed" (every contributor signed the same shares, shares summing to 10000) in Ada as Eve + COBOL as Sutra witnessed
- - `mix_level`: (critical) loudness of the final mix (integrated level and true peak in tenths of a dB within the caps) in Ada as Eve + APL as KRON witnessed
- - `synth`: audio synthesis for click and guide tones (integer sine table, samples per period) in FORTRAN as Trebago
- - `score`: score arrays (bars x parts x events) in APL as KRON
- - `versions`: song version tree (take, stem, editor) in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two collaborators keep different takes of bar 9 to 16: the passage is BOTH
- the test asserts state 3 and that both takes stay live in the session. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `grade_band` | num | `value b1 b2 b3 b4` | kron | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id theorbo
matrix 348
name Theorbo
needs fs.read
needs fs.write
needs mic
needs peers
needs net
needs ledger.read
needs ledger.post
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

