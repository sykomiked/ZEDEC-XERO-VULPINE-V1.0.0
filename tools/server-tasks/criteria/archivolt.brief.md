# archivolt — the complete brief

| | |
|---|---|
| matrix layer | 322 |
| icon group | L3 |
| wave spec | `wave27/322-archivolt.md` |

## Purpose

migrates old systems to ZXV: reads mainframe data and code, translates it, and runs old and new side by side (`files.read`, `files.write`, `play.run`, `xlate.detect`, `net.command`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `ai.ask`
- `net`
- `peers`
- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | COBOL record mapping | primary | runs |
| FORTRAN | `trebago` | numeric | numeric parity checks | primary | runs |
| Ada | `eve` | limits | cutover limits | primary | runs |
| APL | `kron` | arrays | field mapping grid | primary | runs |
| MUMPS | `myra` | records | legacy hierarchy | primary | runs |
| OCaml | `-` | parse | parsing | primary | planned: needs Lingua front-end |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (OCaml): see docs/LANGUAGE_CATALOGUE.md for this tier
- **python** (Python): the host tier (via the SDK): tooling and glue, exact integers.

## The spec's Logic clauses (what each component computes)

- `cutover_ok` (every parity check TRUE over the declared run window, owner approved) in Ada as Eve + APL as KRON witnessed
- `parity` (old and new numeric results equal, element by element) in FORTRAN as Trebago + COBOL as Sutra witnessed
- `record_map` (old fixed records mapped field by field into COBOL copybook form, refusing a value that does not fit) in COBOL as Sutra
- `fields` mapping grid in APL as KRON
- `legacy` hierarchy in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id archivolt
matrix 322
name Archivolt
needs fs.read
needs fs.write
needs ai.ask
needs net
needs peers
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

