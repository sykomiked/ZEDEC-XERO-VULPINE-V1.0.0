# moire — the complete brief

| | |
|---|---|
| matrix layer | 380 |
| icon group | L3 |
| wave spec | `wave32/380-moire.md` |

## Purpose

Adapts any app's look, language and input to each person: simple mode (`ui.get`, `ui.set`), languages (`xlate.languages`, `xlate.detect`, `xlate.translate`), preferences in the Hoard (`files.read`, `files.write`). Display filters and input methods have no method (as Tint and Glyphwright): use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `fs.read`
- `fs.write`
- `ai.ask`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | licence records | primary | runs |
| FORTRAN | `trebago` | numeric | contrast maths | primary | runs |
| Ada | `eve` | limits | readability minimums | primary | runs |
| APL | `kron` | arrays | layout arrays | primary | runs |
| MUMPS | `myra` | records | preference tree | primary | runs |
| Raku | `-` | parse | parsing | primary | planned: needs Lingua front-end |
| Ruby | `ruby` | scripting | scripting | primary | planned: SDK host only, no in-app runtime yet |
| Mojo | `-` | ai | AI | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Raku): see docs/LANGUAGE_CATALOGUE.md for this tier
- **ruby** (Ruby): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `readable`: (critical) readability minimums (text at least the person's size, contrast ratio at least their mark, line length within range) in Ada as Eve + APL as KRON witnessed
- - `contrast`: (critical) contrast maths (relative luminance from an integer sRGB table, ratio x100) in FORTRAN as Trebago + APL as KRON witnessed
- - `layout`: layout arrays (elements x size and order for the person's profile) in APL as KRON
- - `parse_pref`: preference rules joined per app (bounded keys) in APL as KRON
- - `licences`: licence records for fonts and voices (fixed records, refusing on overflow) in COBOL as Sutra
- - `prefs`: preference tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): high contrast and colour coding conflict for one chart: the display is BOTH
- the test asserts state 3 and that both needs are kept (patterns added to the high-contrast colours). Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `within_range` | truth | `value min max` | eve | `check within_range value=0,50,100,101 min=0,0,0,0 max=100,100,100,100 = true,true,true,false` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `clamp` | num | `value min max` | kron | `check clamp value=-5,0,50,150 min=0,0,0,0 max=100,100,100,100 = 0,0,50,100` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id moire
matrix 380
name Moire
needs sys.read
needs fs.read
needs fs.write
needs ai.ask
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

