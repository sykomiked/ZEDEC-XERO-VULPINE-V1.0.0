# crier — the complete brief

| | |
|---|---|
| matrix layer | 156 |
| icon group | L2 |
| wave spec | `wave12/156-crier.md` |

## Purpose

broadcasts one announcement to feeds (`social.post`), chat rooms (`chat.post`, `pspace.post`), radio (`harmonic.encode`, `ehop.bands`) and captions at once.

## Capabilities (the exact registry names for `needs`)

- `peers`
- `net`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | notice register | primary | runs |
| FORTRAN | `trebago` | numeric | reach estimates | primary | runs |
| Ada | `eve` | limits | rate limits | primary | runs |
| APL | `kron` | arrays | channel matrix | primary | runs |
| MUMPS | `myra` | records | audience tree | primary | runs |
| Gleam | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| Mojo | `-` | ai | AI | primary | planned: needs Lingua front-end |
| Icon | `-` | text | text | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Gleam): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "may broadcast to this channel" = the sender may post there AND within the channel's rate limit AND the text fits it (radio frame bytes, caption length) in Ada as Eve + APL as KRON witnessed
- rate limits (at most n a channel an hour) in Ada as Eve
- notice register (notice number, sends per channel) refused on overflow in COBOL as Sutra
- reach (members known per channel, summed) exact in FORTRAN as Trebago
- channel matrix (notice by channel: sent, held) in APL as KRON
- audience tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |

## The manifest contract

```
zxapp 1
id crier
matrix 156
name Crier
needs peers
needs net
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

