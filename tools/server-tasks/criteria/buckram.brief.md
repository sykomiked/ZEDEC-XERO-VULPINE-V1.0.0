# buckram — the complete brief

| | |
|---|---|
| matrix layer | 377 |
| icon group | L3 |
| wave spec | `wave31/377-buckram.md` |

## Purpose

Hardens devices and edge sensors: firmware checks and update rollouts (`update.check`), the fleet's links (`net.peers`, `devmesh.devices`, `devmesh.revoke`), failure reports (`log.tail`, `journal.query`), machine facts (`sys.info`). Firmware reads from other devices and tamper seals have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `net`
- `peers`
- `sys.read`
- `journal`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | device warranties | primary | runs |
| FORTRAN | `trebago` | numeric | failure rate model | primary | runs |
| Ada | `eve` | limits | firmware limits | primary | runs |
| APL | `kron` | arrays | fleet grid | primary | runs |
| MUMPS | `myra` | records | device tree | primary | runs |
| B | `-` | machine | low-level system | primary | planned: needs Lingua front-end |
| Forth | `forth` | stack | small interpreters | primary | runs |
| RPG | `-` | report | reports | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (B): see docs/LANGUAGE_CATALOGUE.md for this tier
- **forth** (Forth): the run tier: stack words. '<entry> ( args -- result )' with integer words. Range laws are natural here.

## The spec's Logic clauses (what each component computes)

- - `rollout_ok`: (critical) firmware limits (signed by both keys AND version above the device's floor AND within the staged rollout share) in Ada as Eve + APL as KRON witnessed
- - `failures`: (critical) failure rate model (failures per 1000 device-days by model) in FORTRAN as Trebago + APL as KRON witnessed
- - `seal`: seal check on the device (seal counter and boot code, small interpreter) in Forth (twin Cairndrake)
- - `fleet`: fleet grid in APL as KRON
- - `warranty`: device warranties (claims and cost in minor units, refusing on overflow) in COBOL as Sutra
- - `devices`: device tree in MUMPS as MYRA
- - Forth and BASIC blocks are committed in twin form: the manifest's logic line names the twin (`logic cairndrake ...`, `logic hatchling ...`)
- Paraconsistent (BOTH test, in test_zxapp.c): a device reports healthy while its seal says tampered: it is BOTH
- the test asserts state 3 and that the device is isolated (revocation proposed to the owner), neither trusted nor wiped. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id buckram
matrix 377
name Buckram
needs net
needs peers
needs sys.read
needs journal
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

