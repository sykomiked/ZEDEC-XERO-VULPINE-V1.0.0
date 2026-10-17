# portcullis — the complete brief

| | |
|---|---|
| matrix layer | 336 |
| icon group | L3 |
| wave spec | `wave28/336-portcullis.md` |

## Purpose

controls entry to venues and communities: invitations, tickets, guest lists and who may come in (`web4.verify`, `net.peers`, `files.read`, `files.write`).

## Capabilities (the exact registry names for `needs`)

- `net`
- `peers`
- `fs.read`
- `fs.write`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | ticket sales | primary | runs |
| FORTRAN | `trebago` | numeric | crowd flow model | primary | runs |
| Ada | `eve` | limits | capacity limits | primary | runs |
| APL | `kron` | arrays | entry grid | primary | runs |
| MUMPS | `myra` | records | guest hierarchy | primary | runs |
| LISP | `lisp` | lists | lists | primary | runs |
| Zig | `-` | machine | low-level system | primary | planned: needs Lingua front-end |
| WebAssembly | `wasm` | sandbox | sandboxing | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **lisp** (LISP): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (Zig): see docs/LANGUAGE_CATALOGUE.md for this tier
- **wasm** (WebAssembly): the host tier: the witnessed circuit; the same computation compiled to a WebAssembly module the checks can witness.

## The spec's Logic clauses (what each component computes)

- `admit` (ticket valid AND on the list AND venue under capacity) in Ada as Eve + APL as KRON witnessed
- `ticket_code` (ticket code well formed, bounded bytes and check digit) in Rust wasm + APL as KRON witnessed
- `crowd_flow` (people per minute through each gate) in FORTRAN as Trebago + APL as KRON witnessed
- `ticket_sales` (in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `entry` grid in APL as KRON
- `guests` hierarchy in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `clamp` | num | `value min max` | kron | `check clamp value=-5,0,50,150 min=0,0,0,0 max=100,100,100,100 = 0,0,50,100` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id portcullis
matrix 336
name Portcullis
needs net
needs peers
needs fs.read
needs fs.write
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

