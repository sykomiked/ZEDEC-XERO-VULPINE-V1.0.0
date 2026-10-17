# fulgurite — the complete brief

| | |
|---|---|
| matrix layer | 245 |
| icon group | L2 |
| wave spec | `wave20/245-fulgurite.md` |

## Purpose

ZXV builds ZXV. Fronts build_system/mk_golden_disc.sh, build_system/golden.contents, build_system/golden.models, build_system/test_golden_disc.sh, kernel/src/zxpkg (zxrelease) and kernel/src/provenance. Runs build and test templates (`play.run`), reads sources and writes artefacts and the bill of materials (`files.read`, `files.write`), versions (`backup.versions`), room on disk (`sys.disk`), publishes the bill of materials by CID (`aipi.put`, `web4.cid`), checks for updates (`update.check`). A build-run method and image signing have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `sys.read`
- `backup`
- `net`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | release ledger | primary | runs |
| FORTRAN | `trebago` | numeric | build timing | primary | runs |
| Ada | `eve` | limits | refuses an unpinned download | primary | runs |
| APL | `kron` | arrays | reproducibility diff grid | primary | runs |
| MUMPS | `myra` | records | bill-of-materials tree | primary | runs |
| ML | `-` | proof | proofs of correctness | primary | planned: needs Lingua front-end |
| ALGOL 60 | `algol60` | proof | proofs of correctness | witness | runs |
| Bash | `-` | jobs | jobs | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (ML): see docs/LANGUAGE_CATALOGUE.md for this tier
- **algol60** (ALGOL 60): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) "reproducible" (two builds' artefact byte vectors equal) in APL as KRON + COBOL as Sutra witnessed (COBOL as Sutra counts differing bytes, refusing on overflow)
- - (critical) "may sign" = reproducible AND every test TRUE (joined with `lpres_conjoin` in C) AND the mutation gate passed AND the owner approved, in Ada as Eve + APL as KRON witnessed
- - (critical) "refuse an unpinned download" (every dependency names a sha256 content id) in Ada as Eve + APL as KRON witnessed
- - build timing (stage ms, totals) exact in FORTRAN as Trebago
- - release ledger (release numbers and artefact byte totals, refusing on overflow) in COBOL as Sutra
- - bill-of-materials tree (component, version, licence, CID) in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two builds of the same source whose images differ in one byte give BOTH
- the test uses two fixed image files under build/ differing at one offset and asserts state 3 and that the image is not signed. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id fulgurite
matrix 245
name Fulgurite
needs fs.read
needs fs.write
needs sys.read
needs backup
needs net
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

