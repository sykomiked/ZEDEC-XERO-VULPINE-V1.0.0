# albedo — the complete brief

| | |
|---|---|
| matrix layer | 276 |
| icon group | L3 |
| wave spec | `wave23/276-albedo.md` |

## Purpose

checks news claims against sources (`search.query`, `files.read`, `web4.feed`, `web4.verify`), showing which are confirmed, refuted or still disputed between credible witnesses.

## Capabilities (the exact registry names for `needs`)

- `search`
- `fs.read`
- `net`
- `journal`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | source subscriptions | primary | runs |
| FORTRAN | `trebago` | numeric | credibility scores | primary | runs |
| Ada | `eve` | limits | publish limits | primary | runs |
| APL | `kron` | arrays | claim matrix | primary | runs |
| MUMPS | `myra` | records | citation tree | primary | runs |
| ML | `-` | proof | proofs of correctness | primary | planned: needs Lingua front-end |
| ALGOL 60 | `algol60` | proof | proofs of correctness | witness | runs |
| Prolog | `-` | rules | rules | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (ML): see docs/LANGUAGE_CATALOGUE.md for this tier
- **algol60** (ALGOL 60): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `claim_state` (each credible source's verdict on a claim joined with `lpres_disjoin` in C
- KRON counts sources per value) with `publish_ok` (a verdict is published only with at least two credible sources named, and a disputed claim says so) in Ada as Eve + APL as KRON witnessed
- `credibility` (a source's record of confirmed over total claims x10000) in FORTRAN as Trebago + APL as KRON witnessed
- `subscriptions` (source subscriptions in minor units) in COBOL as Sutra
- `claims` matrix in APL as KRON
- `citations` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |

## The manifest contract

```
zxapp 1
id albedo
matrix 276
name Albedo
needs search
needs fs.read
needs net
needs journal
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

