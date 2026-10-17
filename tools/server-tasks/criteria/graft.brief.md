# graft — the complete brief

| | |
|---|---|
| matrix layer | 124 |
| icon group | L1 |
| wave spec | `wave10/124-graft.md` |

## Purpose

commits, history and merge of folders in the Hoard; file versions from `backup.versions` and `backup.list`, bring one back with `backup.restore` (never over what is there); work through `files.*`. zxvd has no commit, branch or diff method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `backup`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | money and ledgers | primary | runs |
| FORTRAN | `trebago` | numeric | exact numerics | primary | runs |
| Ada | `eve` | limits | limits | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| Elm | `-` | types | typed data | primary | planned: needs Lingua front-end |
| OCaml | `-` | parse | parsing | primary | planned: needs Lingua front-end |
| Bourne shell | `-` | jobs | jobs | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Elm): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- history intact (each commit's parent index equals the previous commit's) in kron + sutra witnessed
- merge per line as four values (ours changed, theirs changed, same or not: equal change TRUE, one side only TRUE, conflicting BOTH kept for the person, untouched NEITHER) in eve + kron witnessed, joined with `lpres_conjoin` order, never re-derived
- lines added and removed exact in trebago
- limits (file at most 16 MiB to diff, at most 65536 lines) in eve
- commit record (id, parent, author, time, message length) in myra
- A BOTH is never resolved by the app
- the person picks

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## The manifest contract

```
zxapp 1
id graft
matrix 124
name Graft
needs fs.read
needs fs.write
needs backup
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

