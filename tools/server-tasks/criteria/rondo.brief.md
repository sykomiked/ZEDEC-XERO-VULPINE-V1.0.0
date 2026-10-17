# rondo — the complete brief

| | |
|---|---|
| matrix layer | 370 |
| icon group | L3 |
| wave spec | `wave31/370-rondo.md` |

## Purpose

Tracks personal cycles and rhythms privately, with predictions, symptoms and shared care notes, all in the Hoard (`files.read`, `files.write`); quiet hours (`screen.status`, `notes.list`); sharing only with people the person names.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `screen.time`
- `sys.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | clinic billing | primary | runs |
| FORTRAN | `trebago` | numeric | cycle prediction | primary | runs |
| Ada | `eve` | limits | privacy limits | primary | runs |
| APL | `kron` | arrays | day grid | primary | runs |
| MUMPS | `myra` | records | health tree | primary | runs |
| Erlang | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| Mojo | `-` | ai | AI | primary | planned: needs Lingua front-end |
| Perl | `-` | text | text | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Erlang): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `share_ok`: (critical) privacy limits (nothing leaves the device unless the person named the reader AND the item kind) in Ada as Eve + APL as KRON witnessed
- - `predict`: (critical) cycle prediction (mean and spread of the last n cycle lengths in days, the next window) in FORTRAN as Trebago + APL as KRON witnessed
- - `days`: day grid (days x symptoms) in APL as KRON
- - `notes`: shared care notes (bounded text, kept in C) counted per reader in APL as KRON
- - `billing`: clinic billing (minor units, refusing on overflow) in COBOL as Sutra
- - `health`: health tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the predicted start is day 28 and the logged start day 31: the cycle is BOTH
- the test asserts state 3 and that the person's log stands and the prediction shows its uncertainty. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id rondo
matrix 370
name Rondo
needs fs.read
needs fs.write
needs screen.time
needs sys.read
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

