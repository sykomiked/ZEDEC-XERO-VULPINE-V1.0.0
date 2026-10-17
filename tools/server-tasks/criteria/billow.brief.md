# billow — the complete brief

| | |
|---|---|
| matrix layer | 238 |
| icon group | L2 |
| wave spec | `wave19/238-billow.md` |

## Purpose

Broadcasts live audio shows to the mesh with captions and archives: the show plan and segments (`stream.plan`, `stream.segment`), a listening hall (`hall.load`, `hall.play`, `hall.pause`, `hall.status`), the show's space (`pspace.create`, `pspace.post`), captions translated (`xlate.translate`), the archive (`files.write`, `aipi.put`, `web4.cid`). Live audio capture has no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `mic`
- `peers`
- `net`
- `fs.read`
- `fs.write`
- `ai.ask`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | airtime and rights log | primary | runs |
| FORTRAN | `trebago` | numeric | audio levels | primary | runs |
| Ada | `eve` | limits | loudness limits | primary | runs |
| APL | `kron` | arrays | schedule grid | primary | runs |
| MUMPS | `myra` | records | episode tree | primary | runs |
| Go | `go` | messaging | messaging | primary | planned: SDK host only, no in-app runtime yet |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |
| AWK | `-` | text | text | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **go** (Go): see docs/LANGUAGE_CATALOGUE.md for this tier
- **python** (Python): the host tier (via the SDK): tooling and glue, exact integers.
- **-** (AWK): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) loudness limits (integrated level target and true-peak cap in tenths of a dB) in Ada as Eve + APL as KRON witnessed
- - audio levels (mean square over a block in integers) in FORTRAN as Trebago
- - schedule grid (days x slots, no two shows overlap) in APL as KRON
- - airtime and rights log (airtime minutes per show, rights fees in minor units, refusing on overflow) in COBOL as Sutra
- - episode tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the caption says "fifteen" and the aligned transcript "fifty", giving BOTH
- the test asserts state 3 and that the archived transcript is held for correction, not published. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `grade_band` | num | `value b1 b2 b3 b4` | kron | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id billow
matrix 238
name Billow
needs mic
needs peers
needs net
needs fs.read
needs fs.write
needs ai.ask
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

