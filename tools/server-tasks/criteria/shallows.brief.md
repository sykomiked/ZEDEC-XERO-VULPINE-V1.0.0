# shallows — the complete brief

| | |
|---|---|
| matrix layer | 241 |
| icon group | L2 |
| wave spec | `wave20/241-shallows.md` |

## Purpose

Translates a live conversation between two people with captions on both sides: language and translation (`xlate.detect`, `xlate.languages`, `xlate.translate`), the call (`call.start`, `call.status`, `call.signal`), the glossary in the Hoard (`files.read`). Speech recognition and speech output have no method (as Parley): use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `mic`
- `peers`
- `ai.ask`
- `fs.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | glossary records | primary | runs |
| FORTRAN | `trebago` | numeric | confidence scoring | primary | runs |
| Ada | `eve` | limits | latency limits | primary | runs |
| APL | `kron` | arrays | phrase matrix | primary | runs |
| MUMPS | `myra` | records | language tree | primary | runs |
| Mojo | `-` | ai | AI | primary | planned: needs Lingua front-end |
| Raku | `-` | parse | parsing | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Mojo): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) latency limits (a caption later than 2000 ms end to end is marked late, never shown as live) in Ada as Eve + APL as KRON witnessed
- - (critical) "show one meaning" only when the best sense's confidence x100 clears the threshold and the next is at least 20 points below it, in FORTRAN as Trebago + APL as KRON witnessed
- - phrase matrix (phrases x senses, alignment counts) in APL as KRON
- - glossary records and the language tree in MUMPS as MYRA
- - Sutra is left out: no money or counted business records
- the glossary is MYRA's
- Paraconsistent (BOTH test, in test_zxapp.c): "bank" with two senses both above the threshold and within 20 points gives BOTH
- the test asserts state 3 and that both meanings are shown to the speakers to clarify. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id shallows
matrix 241
name Shallows
needs mic
needs peers
needs ai.ask
needs fs.read
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

