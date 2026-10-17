# hamlet — the complete brief

| | |
|---|---|
| matrix layer | 96 |
| icon group | L1 |
| wave spec | `wave4/096-hamlet.md` |

## Purpose

the circle's posts, replies and reactions (`social.feed`, `social.post`, `social.react`, `social.groups`, `social.join`, `social.leave`); gifts to a member through `web4.post` with amount_minor, held until `web4.consent`.

## Capabilities (the exact registry names for `needs`)

- `peers`
- `net`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| FORTRAN | `trebago` | numeric | exact numerics | primary | runs |
| COBOL | `sutra` | money | money and ledgers | primary | runs |
| C++ | `cpp` | money | money and ledgers | witness | runs (compiled to wasm) |
| Erlang | `-` | messaging | messaging | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **cpp** (C++): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (Erlang): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- gift amount (minor units, within the consent ceiling) in sutra + C++ wasm witnessed
- "show this post to this seat" (group member, not muted) in eve + myra witnessed
- unread counts per group in trebago
- read markers in myra. Feed ranking stays the daemon's
- A gift is a financial action: the owner approves it and the receiver consents

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## The manifest contract

```
zxapp 1
id hamlet
matrix 96
name Hamlet
needs peers
needs net
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

