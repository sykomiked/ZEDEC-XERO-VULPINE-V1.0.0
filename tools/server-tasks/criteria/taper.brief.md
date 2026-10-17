# taper — the complete brief

| | |
|---|---|
| matrix layer | 198 |
| icon group | L2 |
| wave spec | `wave16/198-taper.md` |

## Purpose

guards rest: quiet hours (`screen.status`, `screen.check`, `notes.list`), sleep tracking and gentle night reading from the Hoard (`files.read`), time from `sys.info`.

## Capabilities (the exact registry names for `needs`)

- `notify`
- `screen.time`
- `fs.read`
- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | sleep records | primary | runs |
| FORTRAN | `trebago` | numeric | rhythm analysis | primary | runs |
| Ada | `eve` | limits | wake limits | primary | runs |
| APL | `kron` | arrays | night grid | primary | runs |
| MUMPS | `myra` | records | household tree | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.

## The spec's Logic clauses (what each component computes)

- "may wake now" (nothing before the declared wake time unless it is an emergency) in Ada as Eve + APL as KRON witnessed
- sleep midpoint mean and spread over 14 nights in FORTRAN as Trebago + APL as KRON witnessed
- night grid in APL as KRON
- household tree in MUMPS as MYRA

## The entries and their exact checks

(no library pattern matched; author the entries from the clauses)

## The manifest contract

```
zxapp 1
id taper
matrix 198
name Taper
needs notify
needs screen.time
needs fs.read
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

