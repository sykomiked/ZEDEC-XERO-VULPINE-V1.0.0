# compositor — the complete brief

| | |
|---|---|
| matrix layer | 165 |
| icon group | L2 |
| wave spec | `wave13/165-compositor.md` |

## Purpose

writes and runs scripts in any pillar language from one workbench (`play.catalogue`, `play.run`, `play.draft`), saved in the Hoard (`files.read`, `files.write`), with debugging built in.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `sys.read`
- `ai.ask`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | job records | primary | runs |
| FORTRAN | `trebago` | numeric | numeric routines | primary | runs |
| Ada | `eve` | limits | contracts | primary | runs |
| APL | `kron` | arrays | array REPL | primary | runs |
| MUMPS | `myra` | records | global trees | primary | runs |
| Ruby | `ruby` | scripting | scripting | primary | planned: SDK host only, no in-app runtime yet |
| Raku | `-` | parse | parsing | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **ruby** (Ruby): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (Raku): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "outputs agree" for one script run in two languages (output vectors equal, element by element) in Ada as Eve + APL as KRON witnessed, joined with Colonnade's verdict
- job records (job number, fuel spent, refused on overflow) in COBOL as Sutra + APL as KRON witnessed
- numeric routines (run time totals) exact in FORTRAN as Trebago
- script contracts (declared input ranges) in Ada as Eve
- the array REPL is a KRON session
- global trees are a MYRA browser

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |

## The manifest contract

```
zxapp 1
id compositor
matrix 165
name Compositor
needs fs.read
needs fs.write
needs sys.read
needs ai.ask
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

