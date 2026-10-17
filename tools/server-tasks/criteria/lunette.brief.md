# lunette — the complete brief

| | |
|---|---|
| matrix layer | 319 |
| icon group | L3 |
| wave spec | `wave26/319-lunette.md` |

## Purpose

records life ceremonies such as weddings, namings and funerals: bookings, officiants, certificates and guests (`files.read`, `files.write`, `social.groups`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `peers`
- `net`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | ceremony fees | primary | runs |
| FORTRAN | `trebago` | numeric | guest capacity | primary | runs |
| Ada | `eve` | limits | legal notice periods | primary | runs |
| APL | `kron` | arrays | venue calendar grid | primary | runs |
| MUMPS | `myra` | records | family tree | primary | runs |
| RPG | `-` | report | reports | primary | planned: needs Lingua front-end |
| Perl | `-` | text | text | primary | planned: needs Lingua front-end |
| TeX | `-` | typesetting | typesetting | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (RPG): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `notice_ok` (the declared legal notice period between filing and the ceremony day is met) in Ada as Eve + APL as KRON witnessed
- `guest_capacity` (guests at most the venue's seats, with the RSVP counts) in FORTRAN as Trebago + APL as KRON witnessed
- `fees` (ceremony fees in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `venues` calendar grid in APL as KRON
- `family` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id lunette
matrix 319
name Lunette
needs fs.read
needs fs.write
needs peers
needs net
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

