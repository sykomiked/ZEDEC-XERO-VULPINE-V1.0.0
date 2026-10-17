# chrysalis — the complete brief

| | |
|---|---|
| matrix layer | 268 |
| icon group | L2 |
| wave spec | `wave22/268-chrysalis.md` |

## Purpose

Fronts guest machines (kernel/src/hwemu with hwe.c and hwa.c, kernel/src/virtio, kernel/src/legacy, kernel/src/capctl; no hypervisor exists yet). The host (`sys.info`, `sys.disk`), energy and budgets (`power.top`, `power.status`, `power.budget`), switches (`features.get`), guest disk images in the Hoard (`files.read`, `files.write`). Creating, starting and pausing a guest, and the driver machine, have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `fs.read`
- `fs.write`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | guest ledger | primary | runs |
| FORTRAN | `trebago` | numeric | CPU and memory shares | primary | runs |
| Ada | `eve` | limits | guest isolation limits | primary | runs |
| APL | `kron` | arrays | guest table | primary | runs |
| MUMPS | `myra` | records | guest disk images | primary | runs |
| Crystal | `-` | machine | low-level system | primary | planned: needs Lingua front-end |
| WebAssembly | `wasm` | sandbox | sandboxing | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Crystal): see docs/LANGUAGE_CATALOGUE.md for this tier
- **wasm** (WebAssembly): the host tier: the witnessed circuit; the same computation compiled to a WebAssembly module the checks can witness.

## The spec's Logic clauses (what each component computes)

- - (critical) CPU and memory shares (guests' shares plus the host reserve within the machine) in FORTRAN as Trebago + APL as KRON witnessed
- - (critical) "guest isolated" (no shared memory beyond its declared rings, no device passed without the owner) in Ada as Eve + APL as KRON + C++ wasm (three witnesses
- wasm owns sandboxing here)
- - guest table (guests x CPU, memory, devices) in APL as KRON
- - guest ledger (CPU ms counters, refusing on overflow) in COBOL as Sutra
- - guest disk image records in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): a guest reports itself idle while the host measures it at 100% CPU, giving BOTH
- the test asserts state 3 and that the guest is paused, not trusted. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id chrysalis
matrix 268
name Chrysalis
needs sys.read
needs fs.read
needs fs.write
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

