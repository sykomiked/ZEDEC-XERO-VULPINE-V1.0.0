# weir — the complete brief

| | |
|---|---|
| matrix layer | 237 |
| icon group | L2 |
| wave spec | `wave19/237-weir.md` |

## Purpose

Maps household water: pipes, valves, flow, budgets and leak alerts, kept in the Hoard (`files.read`, `files.write`), event history (`journal.query`, `log.tail`). Water meters and valve actuators have no method (as Understory and Homestead): use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `journal`
- `net`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | utility bills | primary | runs |
| FORTRAN | `trebago` | numeric | flow model | primary | runs |
| Ada | `eve` | limits | pressure limits | primary | runs |
| APL | `kron` | arrays | pipe matrix | primary | runs |
| MUMPS | `myra` | records | fixture tree | primary | runs |
| Forth | `forth` | control | machine control | primary | runs |
| Gleam | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| Scala | `-` | dataflow | dataflow | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **forth** (Forth): the run tier: stack words. '<entry> ( args -- result )' with integer words. Range laws are natural here.
- **-** (Gleam): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) "leak suspected" (flow above zero for at least 120 night minutes, or meter and fixture totals apart by more than the threshold) in Ada as Eve + APL as KRON witnessed
- - (critical) litres from meter pulses (pulses x ml per pulse) in Forth (twin Cairndrake) (the counter's raw-count check at the device) + FORTRAN as Trebago witnessed
- - flow model (litres per period, night window) exact in FORTRAN as Trebago
- - pressure limits (kPa range) in Ada as Eve
- - pipe matrix (pipes x valves x fixtures) in APL as KRON
- - utility bills (cubic metres x tariff bands, minor units, refusing on overflow) in COBOL as Sutra
- - fixture tree in MUMPS as MYRA
- - Forth and BASIC blocks are committed in twin form: the manifest's logic line names the twin (`logic cairndrake ...`, `logic hatchling ...`)
- Paraconsistent (BOTH test, in test_zxapp.c): the meter reads 412 L for the day and the fixture sensors sum to 300 L, giving BOTH
- the test asserts state 3 and the likely-leak notice naming the gap. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `grade_band` | num | `value b1 b2 b3 b4` | kron | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id weir
matrix 237
name Weir
needs fs.read
needs fs.write
needs journal
needs net
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

