# watershed — the complete brief

| | |
|---|---|
| matrix layer | 221 |
| icon group | L2 |
| wave spec | `wave18/221-watershed.md` |

## Purpose

Runs cooperative projects: the pooled fund and member shares in the books (`ledger.accounts`, `ledger.balance`, `ledger.entries`, `ledger.post`), escrow for disputed amounts (`ledger.hold`, `ledger.holds`, `ledger.release`), member votes (`spaces.propose`, `spaces.vote`, `spaces.get`), posts (`social.post`, `pspace.post`), market sales (`net.offers`, `net.command` sell, `mesh.books`), shared tasks in the Hoard (`files.read`, `files.write`).

## Capabilities (the exact registry names for `needs`)

- `ledger.read`
- `ledger.post`
- `peers`
- `net`
- `fs.read`
- `fs.write`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | member shares | primary | runs |
| FORTRAN | `trebago` | numeric | grant model | primary | runs |
| Ada | `eve` | limits | spending caps | primary | runs |
| APL | `kron` | arrays | contribution grid | primary | runs |
| MUMPS | `myra` | records | member tree | primary | runs |
| Gleam | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| LISP | `lisp` | lists | lists | primary | runs |
| Prolog | `-` | rules | rules | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Gleam): see docs/LANGUAGE_CATALOGUE.md for this tier
- **lisp** (LISP): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) member shares of the pool (largest remainder, summing exactly to the pool) in COBOL as Sutra + FORTRAN as Trebago witnessed
- - (critical) a project grant from the pool by approved weight, remainder carried, in FORTRAN as Trebago + COBOL as Sutra witnessed
- - (critical) "spend allowed" = vote carried AND under the spending cap AND balance covers it, in Ada as Eve + COBOL as Sutra witnessed
- - contribution grid (members x months) in APL as KRON
- - member tree (member, role, joined, shares) in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): a member's record says 500 paid while `ledger.entries` shows 0 for that member, giving BOTH
- the test (scratch books) asserts state 3, the 500 placed on `ledger.hold`, no change to member shares, and the co-op's other posts still allowed. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id watershed
matrix 221
name Watershed
needs ledger.read
needs ledger.post
needs peers
needs net
needs fs.read
needs fs.write
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

