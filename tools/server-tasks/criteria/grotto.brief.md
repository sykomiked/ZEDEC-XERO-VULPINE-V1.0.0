# grotto — the complete brief

| | |
|---|---|
| matrix layer | 263 |
| icon group | L2 |
| wave spec | `wave21/263-grotto.md` |

## Purpose

Keeps a hidden, encrypted compartment for the most sensitive files: a private space (`spaces.create`, `spaces.get`), files moved in only through `files.cross` and kept with `files.*` (`files.list`, `files.read`, `files.write`), unlock by passkey (`passkeys.auth_begin`, `passkeys.auth_finish`), the seat locked on leaving (`seats.lock`). Encrypting a compartment at rest has no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `sys.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | access records | primary | runs |
| FORTRAN | `trebago` | numeric | key derivation | primary | runs |
| Ada | `eve` | limits | unlock limits | primary | runs |
| APL | `kron` | arrays | block map | primary | runs |
| MUMPS | `myra` | records | folder tree | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.

## The spec's Logic clauses (what each component computes)

- - (critical) "may unlock now" (attempts within the limit, back-off over, this seat's passkey) in Ada as Eve + APL as KRON witnessed
- - key derivation cost (memory KiB x passes within the time budget
- parameters only, never key bytes) exact in FORTRAN as Trebago
- - block map (compartment blocks x used) in APL as KRON
- - access records (attempts and opens, refusing on overflow) in COBOL as Sutra
- - folder tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): an unlock from this seat and another from a second device at the same moment give BOTH
- the test asserts state 3, the attempt reported, and nothing opened. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id grotto
matrix 263
name Grotto
needs fs.read
needs fs.write
needs sys.read
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

