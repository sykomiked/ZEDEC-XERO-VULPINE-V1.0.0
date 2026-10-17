# sheaf — the complete brief

| | |
|---|---|
| matrix layer | 170 |
| icon group | L2 |
| wave spec | `wave14/170-sheaf.md` |

## Purpose

keeps the farm ledger: fields, harvests, livestock, sales and weather in one place, in the books (`ledger.accounts`, `ledger.balance`, `ledger.entries`, `ledger.post`, `ledger.hold`) and the Hoard (`files.read`, `files.write`).

## Capabilities (the exact registry names for `needs`)

- `ledger.read`
- `ledger.post`
- `fs.read`
- `fs.write`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | farm accounts | primary | runs |
| FORTRAN | `trebago` | numeric | yield model | primary | runs |
| Ada | `eve` | limits | chemical limits | primary | runs |
| APL | `kron` | arrays | field grid | primary | runs |
| MUMPS | `myra` | records | herd tree | primary | runs |
| Forth | `forth` | stack | small interpreters | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **forth** (Forth): the run tier: stack words. '<entry> ( args -- result )' with integer words. Range laws are natural here.

## The spec's Logic clauses (what each component computes)

- farm accounts per field and enterprise in minor units in COBOL as Sutra + FORTRAN as Trebago witnessed
- harvest per hectare per field (kilograms) exact in FORTRAN as Trebago
- "application allowed" (rate at most the label maximum AND the re-entry interval over) in Ada as Eve + APL as KRON witnessed
- scale reading (raw load-cell counts to grams with the calibration pair, range check) in Forth (twin Cairndrake)
- field grid in APL as KRON
- herd tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `within_range` | truth | `value min max` | eve | `check within_range value=0,50,100,101 min=0,0,0,0 max=100,100,100,100 = true,true,true,false` |

## The manifest contract

```
zxapp 1
id sheaf
matrix 170
name Sheaf
needs ledger.read
needs ledger.post
needs fs.read
needs fs.write
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

