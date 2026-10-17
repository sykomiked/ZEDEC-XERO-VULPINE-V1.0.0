# umbra — the complete brief

| | |
|---|---|
| matrix layer | 314 |
| icon group | L3 |
| wave spec | `wave26/314-umbra.md` |

## Purpose

lets you act under separate personas and pseudonyms online while keeping each unlinkable and accountable (`seats.list`, `web4.agent_card`, `web4.card_verify`, `chat.me`).

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `net`
- `peers`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | persona accounts | primary | runs |
| FORTRAN | `trebago` | numeric | linkability scoring | primary | runs |
| Ada | `eve` | limits | disclosure limits | primary | runs |
| APL | `kron` | arrays | persona permission grid | primary | runs |
| MUMPS | `myra` | records | identity tree | primary | runs |
| Smalltalk | `-` | objects | domain objects | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Smalltalk): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `disclose_ok` (a field crosses from one persona to another only with the person's say for that field) in Ada as Eve + APL as KRON witnessed
- `linkability` (shared attributes between two personas, weighted count) in FORTRAN as Trebago + APL as KRON witnessed
- `persona_accounts` (books per persona kept apart, minor units) in COBOL as Sutra
- `permissions` grid (persona by capability) in APL as KRON
- `identity` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `flags_none` | truth | `bits forbidden` | kron | `check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id umbra
matrix 314
name Umbra
needs sys.read
needs net
needs peers
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

