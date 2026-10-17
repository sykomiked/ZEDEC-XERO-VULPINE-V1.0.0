# dragoman — the complete brief

| | |
|---|---|
| matrix layer | 244 |
| icon group | L2 |
| wave spec | `wave20/244-dragoman.md` |

## Purpose

Fronts the language layer (kernel/src/lingua with catalogue.tsv, the 104 languages; kernel/src/legacy (cobol.c, fortran.c); kernel/src/sutra; kernel/src/apps/LANGUAGE_FIT.tsv and `zxapp_lang_of` in kernel/src/apps/zxapp.c for the twin names). Runs each runtime's samples (`play.catalogue`, `play.run` on scratch state, `play.draft` untrusted), lists the daemon (`rpc.discover`, `zxvd.version`), checks for runtime updates (`update.check`), reads files (`files.read`). Installing a runtime has no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `fs.read`
- `net`
- `ai.ask`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | COBOL host | primary | runs |
| FORTRAN | `trebago` | numeric | Fortran host | primary | runs |
| Ada | `eve` | limits | Ada host and runtime limits | primary | runs |
| APL | `kron` | arrays | APL and K host | primary | runs |
| MUMPS | `myra` | records | MUMPS host | primary | runs |
| OCaml | `-` | parse | parsing | primary | planned: needs Lingua front-end |
| Mojo | `-` | ai | AI | primary | planned: needs Lingua front-end |
| Crystal | `-` | machine | low-level system | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (OCaml): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) "this language runs" (runtime column not `-` AND its sample's `play.run` verdict TRUE) counted over the 104 in APL as KRON + FORTRAN as Trebago witnessed
- - (critical) runtime limits (fuel, output at most 49152 bytes, at most 60 s) in Ada as Eve + APL as KRON witnessed
- - each pillar hosts its legacy language's conformance sample: COBOL money rows in COBOL as Sutra, Fortran sums in FORTRAN as Trebago, Ada limits in Ada as Eve, APL and K arrays in APL as KRON, MUMPS records in MUMPS as MYRA
- - language to AI-twin pairing (language, twin, runtime) in MUMPS as MYRA
- - C++ wasm is not used: no runtime row for Dragoman
- Paraconsistent (BOTH test, in test_zxapp.c): one program run in Sutra and in Trebago gives 1001 and 1000, giving BOTH
- the test asserts state 3 and that both results are kept for the developer. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id dragoman
matrix 244
name Dragoman
needs sys.read
needs fs.read
needs net
needs ai.ask
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

