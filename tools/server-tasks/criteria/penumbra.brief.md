# penumbra — the complete brief

| | |
|---|---|
| matrix layer | 284 |
| icon group | L3 |
| wave spec | `wave24/284-penumbra.md` |

## Purpose

hunts quietly for hidden threats across a network the owner declares in scope, replaying traces (`log.tail`, `journal.query`, `sys.net`, `net.peers`) and testing defences before attackers do (`play.run` on scratch state). zxvd has no method for packet capture and trace replay: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `journal`
- `sys.read`
- `net`
- `peers`
- `ai.ask`
- `fs.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | findings ledger | primary | runs |
| FORTRAN | `trebago` | numeric | attack path scoring | primary | runs |
| Ada | `eve` | limits | test scope limits | primary | runs |
| APL | `kron` | arrays | traffic matrices | primary | runs |
| MUMPS | `myra` | records | asset tree | primary | runs |
| Elixir | `-` | messaging | messaging | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Elixir): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `in_scope` (target in the owner's declared scope list AND inside the test window AND not a system process) in Ada as Eve + APL as KRON witnessed
- `attack_path` (path score over the asset graph, summed weights) in FORTRAN as Trebago + APL as KRON witnessed
- `findings` (finding number, severity, never reused) in COBOL as Sutra
- `traffic` matrices in APL as KRON
- `assets` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |

## The manifest contract

```
zxapp 1
id penumbra
matrix 284
name Penumbra
needs journal
needs sys.read
needs net
needs peers
needs ai.ask
needs fs.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

