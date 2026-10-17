# vesper — the complete brief

| | |
|---|---|
| matrix layer | 279 |
| icon group | L3 |
| wave spec | `wave23/279-vesper.md` |

## Purpose

spreads warnings, check-ins and help requests across nearby households even without internet (`ehop.bands`, `ehop.seal`, `harmonic.encode`, `pspace.post`, `chat.post`, `net.peers`).

## Capabilities (the exact registry names for `needs`)

- `peers`
- `net`
- `notify`
- `location`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | volunteer hours | primary | runs |
| FORTRAN | `trebago` | numeric | spread model | primary | runs |
| Ada | `eve` | limits | alert rate limits | primary | runs |
| APL | `kron` | arrays | household status grid | primary | runs |
| MUMPS | `myra` | records | neighbourhood tree | primary | runs |
| Erlang | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| JavaScript | `typescript` | view | view and model | primary | planned: SDK host only, no in-app runtime yet |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Erlang): see docs/LANGUAGE_CATALOGUE.md for this tier
- **typescript** (JavaScript): the host tier: the browser surface, typed.

## The spec's Logic clauses (what each component computes)

- `relay_ok` (a warning is relayed at most once per household per hour unless it changes, within the alert rate limit) in Ada as Eve + APL as KRON witnessed
- `spread` (households reached after n hops from the link matrix) in FORTRAN as Trebago + APL as KRON witnessed
- `volunteer_hours` (hours per volunteer per month, refused on overflow) in COBOL as Sutra
- `households` status grid in APL as KRON
- `neighbourhood` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id vesper
matrix 279
name Vesper
needs peers
needs net
needs notify
needs location
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

