# loupe — the complete brief

| | |
|---|---|
| matrix layer | 181 |
| icon group | L2 |
| wave spec | `wave15/181-loupe.md` |

## Purpose

investigates incidents by tracing logs (`log.tail`), events (`journal.query`, `journal.verify`) and changes to files (`search.query`, `files.stat`) into a chain of evidence.

## Capabilities (the exact registry names for `needs`)

- `journal`
- `search`
- `fs.read`
- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | evidence register | primary | runs |
| FORTRAN | `trebago` | numeric | timeline analysis | primary | runs |
| Ada | `eve` | limits | chain-of-custody rules | primary | runs |
| APL | `kron` | arrays | event matrix | primary | runs |
| MUMPS | `myra` | records | case tree | primary | runs |
| Elixir | `-` | faults | fault handling | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Elixir): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "evidence admissible" (content id unchanged since it was registered AND every custody hand-off recorded AND hand-off times rising) in Ada as Eve + APL as KRON witnessed
- evidence register (item number, custody hand-offs, never reused, refused on overflow) in COBOL as Sutra
- timeline gaps (differences between event times in ms, the largest gap) exact in FORTRAN as Trebago
- event matrix (source by minute counts) in APL as KRON
- case tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## The manifest contract

```
zxapp 1
id loupe
matrix 181
name Loupe
needs journal
needs search
needs fs.read
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

