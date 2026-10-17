# holm — the complete brief

| | |
|---|---|
| matrix layer | 267 |
| icon group | L2 |
| wave spec | `wave22/267-holm.md` |

## Purpose

Locks a device into a hardened offline mode for kiosks or sensitive work: network off (`net.command` off), answering peers stopped (`peer.serve`), the phone link (`phone.link`), switches (`features.get`, `features.set`), simple mode (`ui.get`, `ui.set`), seats (`seats.lock`, `seats.set_auto`).

## Capabilities (the exact registry names for `needs`)

- `net`
- `peers`
- `sys.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | change records | primary | runs |
| FORTRAN | `trebago` | numeric | risk scoring | primary | runs |
| Ada | `eve` | limits | hard limits | primary | runs |
| APL | `kron` | arrays | policy matrix | primary | runs |
| MUMPS | `myra` | records | config tree | primary | runs |
| BCPL | `-` | machine | low-level system | primary | planned: needs Lingua front-end |
| HyperTalk | `-` | view | view and model | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (BCPL): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) "may leave hardened mode" (owner session with passphrase, not a kiosk seat) in Ada as Eve + APL as KRON witnessed
- - (critical) risk score (weighted count of open surfaces) in FORTRAN as Trebago + APL as KRON witnessed
- - policy matrix (profiles x switches) in APL as KRON
- - change records (change numbers, refusing on overflow) in COBOL as Sutra
- - config tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): profile A sets the network off and profile B sets it to lan, giving BOTH
- the test asserts state 3 and that the safer value (off) applies until resolved. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id holm
matrix 267
name Holm
needs net
needs peers
needs sys.read
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

