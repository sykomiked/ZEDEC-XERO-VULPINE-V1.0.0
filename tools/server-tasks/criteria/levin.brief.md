# levin — the complete brief

| | |
|---|---|
| matrix layer | 236 |
| icon group | L2 |
| wave spec | `wave19/236-levin.md` |

## Purpose

Plans home wiring: circuits, load calculations, test runs and safety checks, kept in the Hoard (`files.read`, `files.write`); the machine's own draw from `power.status`. Meter and clamp readings have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `sys.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | permit records | primary | runs |
| FORTRAN | `trebago` | numeric | load and voltage drop | primary | runs |
| Ada | `eve` | limits | breaker limits | primary | runs |
| APL | `kron` | arrays | circuit matrix | primary | runs |
| MUMPS | `myra` | records | panel tree | primary | runs |
| Verilog | `-` | circuit | circuits | primary | planned: needs Lingua front-end |
| Simula I | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| Delphi | `-` | structure | structure | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Verilog): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) "circuit within its breaker" (load at most 80% of rating, cable rating at least the breaker, voltage drop at most the owner's limit) in Ada as Eve + APL as KRON witnessed
- - (critical) load and voltage drop (current in mA from W and V
- drop = 2 x length x current x resistance per metre, integer milliohms) in FORTRAN as Trebago + APL as KRON witnessed
- - circuit matrix (circuits x rooms x loads) in APL as KRON
- - permit records (permit fees in minor units and inspection counts, refusing on overflow) in COBOL as Sutra
- - panel tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the plan puts the kitchen's 2 kW on circuit 3 while the reading shows circuit 3 idle and circuit 4 carrying it, giving BOTH
- the test asserts state 3 and a miswiring flag. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id levin
matrix 236
name Levin
needs fs.read
needs fs.write
needs sys.read
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

