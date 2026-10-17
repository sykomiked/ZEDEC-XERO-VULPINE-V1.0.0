# cleave — the complete brief

| | |
|---|---|
| matrix layer | 310 |
| icon group | L3 |
| wave spec | `wave26/310-cleave.md` |

## Purpose

stress tests apps and systems by breaking them on purpose and recording how and where they fail (`play.run` on scratch state, `sys.info`, `files.write`).

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `fs.read`
- `fs.write`
- `ai.ask`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | test budget | primary | runs |
| FORTRAN | `trebago` | numeric | load generation | primary | runs |
| Ada | `eve` | limits | blast radius limits | primary | runs |
| APL | `kron` | arrays | failure matrix | primary | runs |
| MUMPS | `myra` | records | component tree | primary | runs |
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

- `blast_ok` (a test touches only targets in its declared blast radius, never the books, keys or the Hoard root) in Ada as Eve + APL as KRON witnessed
- `load_gen` (requests per second ramp over time) in FORTRAN as Trebago + APL as KRON witnessed
- `test_budget` (fuel per test run, refused on overflow) in COBOL as Sutra
- `failures` matrix in APL as KRON
- `components` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## The manifest contract

```
zxapp 1
id cleave
matrix 310
name Cleave
needs sys.read
needs fs.read
needs fs.write
needs ai.ask
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

