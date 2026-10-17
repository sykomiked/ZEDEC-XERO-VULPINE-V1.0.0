# lapidary — the complete brief

| | |
|---|---|
| matrix layer | 191 |
| icon group | L2 |
| wave spec | `wave15/191-lapidary.md` |

## Purpose

designs schematics, boards and chip logic in the Hoard (`files.read`, `files.write`), simulates them, and publishes defensive-publication packets under the ZXV Hardware License (`aipi.put`, `web4.cid`), every revision on the journal (`journal.query`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `journal`
- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | design revision ledger | primary | runs |
| FORTRAN | `trebago` | numeric | circuit maths | primary | runs |
| Ada | `eve` | limits | electrical safety limits | primary | runs |
| APL | `kron` | arrays | netlist arrays | primary | runs |
| MUMPS | `myra` | records | part library | primary | runs |
| VHDL | `-` | circuit | circuits | primary | planned: needs Lingua front-end |
| Zig | `-` | machine | low-level system | primary | planned: needs Lingua front-end |
| Mathematica | `-` | symbolic | symbolic reasoning | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (VHDL): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- divider and current (Ohm's law as reduced rationals in mV and mA) in FORTRAN as Trebago + APL as KRON witnessed
- "within electrical safety limits" (trace current at most its width's rating, voltage at most the clearance's) in Ada as Eve + APL as KRON witnessed
- "may publish" (revision recorded AND simulation and measurement agree AND licence is the ZXV Hardware License) in Ada as Eve + APL as KRON witnessed
- design revision ledger (revision numbers rising, never reused) in COBOL as Sutra
- netlist arrays (every pin on exactly one net) in APL as KRON
- part library in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |

## The manifest contract

```
zxapp 1
id lapidary
matrix 191
name Lapidary
needs fs.read
needs fs.write
needs journal
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

