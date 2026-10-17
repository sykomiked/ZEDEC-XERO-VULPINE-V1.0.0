# icehouse — the complete brief

| | |
|---|---|
| matrix layer | 194 |
| icon group | L2 |
| wave spec | `wave16/194-icehouse.md` |

## Purpose

freezes snapshots of systems and data into cold archives (`backup.run`, `backup.list`, `backup.verify`) and thaws them on demand (`backup.restore`), within the free space `sys.disk` reports. zxvd has no method for an archive codec (as Hamper reports): use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `backup`
- `fs.read`
- `fs.write`
- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | archive catalogue | primary | runs |
| FORTRAN | `trebago` | numeric | compression ratios | primary | runs |
| Ada | `eve` | limits | integrity limits | primary | runs |
| APL | `kron` | arrays | block arrays | primary | runs |
| MUMPS | `myra` | records | snapshot tree | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.

## The spec's Logic clauses (what each component computes)

- "thawed copy trusted" (size and content id equal the recorded ones) in Ada as Eve + APL as KRON witnessed
- compression ratio x100 (packed x 10000 / unpacked) in FORTRAN as Trebago + APL as KRON witnessed
- archive catalogue (archive number, bytes, refused on overflow) in COBOL as Sutra
- block arrays in APL as KRON
- snapshot tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |

## The manifest contract

```
zxapp 1
id icehouse
matrix 194
name Icehouse
needs backup
needs fs.read
needs fs.write
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

