# tintype — the complete brief

| | |
|---|---|
| matrix layer | 167 |
| icon group | L2 |
| wave spec | `wave13/167-tintype.md` |

## Purpose

captures, develops and catalogues photos and screen grabs in one workflow in the Hoard (`files.list`, `files.stat`, `files.write`), within the free space `sys.disk` reports. zxvd has no method for image decode (as Gridmark reports), display capture (as Glimpse reports): use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `camera`
- `fs.read`
- `fs.write`
- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | rights register | primary | runs |
| FORTRAN | `trebago` | numeric | exposure math | primary | runs |
| Ada | `eve` | limits | storage limits | primary | runs |
| APL | `kron` | arrays | image arrays | primary | runs |
| MUMPS | `myra` | records | album tree | primary | runs |
| Julia | `-` | matrix | matrices and images | primary | planned: needs Lingua front-end |
| Clojure | `-` | dataflow | dataflow | primary | planned: needs Lingua front-end |
| JavaScript | `typescript` | view | view and model | primary | planned: SDK host only, no in-app runtime yet |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Julia): see docs/LANGUAGE_CATALOGUE.md for this tier
- **typescript** (JavaScript): the host tier: the browser surface, typed.

## The spec's Logic clauses (what each component computes)

- exposure value from aperture, shutter and ISO (integer log2 tables, thirds of a stop) in FORTRAN as Trebago + APL as KRON witnessed
- "possibly altered" (recorded width, height, time or content id differ from what the file decodes to) in Ada as Eve + APL as KRON witnessed
- rights register (photo, owner, licence code, release flag) as fixed records in COBOL as Sutra
- storage limits (album at most the declared bytes) in Ada as Eve
- develop curves over image arrays in APL as KRON
- album tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## The manifest contract

```
zxapp 1
id tintype
matrix 167
name Tintype
needs camera
needs fs.read
needs fs.write
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

