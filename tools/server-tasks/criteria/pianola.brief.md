# pianola — the complete brief

| | |
|---|---|
| matrix layer | 354 |
| icon group | L3 |
| wave spec | `wave29/354-pianola.md` |

## Purpose

Builds, tests and ships apps in all five pillars from one workspace: scripts and runs (`play.catalogue`, `play.run`, `play.draft`), sources in the Hoard (`files.read`, `files.write`, `search.query`), releases by CID (`aipi.put`, `web4.cid`), versions (`backup.versions`). Build runs and signing have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `search`
- `sys.read`
- `ai.ask`
- `backup`
- `net`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | release billing | primary | runs |
| FORTRAN | `trebago` | numeric | numeric kernels | primary | runs |
| Ada | `eve` | limits | contracts and limits | primary | runs |
| APL | `kron` | arrays | test arrays | primary | runs |
| MUMPS | `myra` | records | module tree | primary | runs |
| Haskell | `-` | proof | proofs of correctness | primary | planned: needs Lingua front-end |
| ML | `-` | proof | proofs of correctness | witness | planned: needs Lingua front-end |
| Bash | `-` | jobs | jobs | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Haskell): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `ship_ok`: (critical) "may ship" (every pillar's witnessed run agrees AND Fulgurite's `may sign` TRUE AND the owner approved) in Ada as Eve + APL as KRON witnessed, joined with `lpres_conjoin` in C
- - `contract`: (critical) contracts and limits per function (declared input ranges hold on every test row) in Ada as Eve + APL as KRON witnessed
- - `kernels`: numeric kernels under test (sums and rationals) in FORTRAN as Trebago
- - `tests`: test arrays (functions x languages x rows, disagreements found) in APL as KRON
- - `run_jobs`: test run order (modules after their dependencies) in APL as KRON
- - `billing`: release billing (build minutes and fees in minor units, refusing on overflow) in COBOL as Sutra
- - `modules`: module tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the Sutra and Trebago versions of one function return 1001 and 1000 on the same row: the function is BOTH
- the test asserts state 3 and that it is shown, and `ship_ok` refuses. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id pianola
matrix 354
name Pianola
needs fs.read
needs fs.write
needs search
needs sys.read
needs ai.ask
needs backup
needs net
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

