# stereoscope — the complete brief

| | |
|---|---|
| matrix layer | 350 |
| icon group | L3 |
| wave spec | `wave29/350-stereoscope.md` |

## Purpose

Shoots, edits and publishes photo stories with signed originals, tracked edits and licences sold per image: photos in the Hoard (`files.read`, `files.write`, `sys.disk`), published by CID (`aipi.put`, `web4.cid`), signatures checked (`web4.verify`), licence sales in the books (`ledger.post`, `ledger.entries`).

## Capabilities (the exact registry names for `needs`)

- `camera`
- `fs.read`
- `fs.write`
- `sys.read`
- `net`
- `ledger.read`
- `ledger.post`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | licence sales | primary | runs |
| FORTRAN | `trebago` | numeric | colour science | primary | runs |
| Ada | `eve` | limits | exposure limits | primary | runs |
| APL | `kron` | arrays | pixel arrays | primary | runs |
| MUMPS | `myra` | records | shoot album tree | primary | runs |
| MATLAB | `-` | matrix | matrices and images | primary | planned: needs Lingua front-end |
| ML | `-` | proof | proofs of correctness | primary | planned: needs Lingua front-end |
| ALGOL 60 | `algol60` | proof | proofs of correctness | witness | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (MATLAB): see docs/LANGUAGE_CATALOGUE.md for this tier
- **algol60** (ALGOL 60): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `edited_mark`: (critical) "show as edited" (the edit's content id differs from the signed original's) in Ada as Eve + APL as KRON witnessed
- - `licence_sale`: (critical) licence sale (price per use kind, minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- - `colour`: colour science (white balance gains, integer 3x3 matrices q12) in FORTRAN as Trebago
- - `exposure_ok`: exposure limits (shutter, ISO within the camera's range) in Ada as Eve
- - `pixels`: pixel arrays (edits as whole-row operations) in APL as KRON
- - `albums`: shoot album tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): an edit and its signed original are both shown for one image: the image's state is BOTH (original TRUE, edit differs)
- the test asserts state 3 and that viewers see the edited mark and can open the original. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id stereoscope
matrix 350
name Stereoscope
needs camera
needs fs.read
needs fs.write
needs sys.read
needs net
needs ledger.read
needs ledger.post
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

