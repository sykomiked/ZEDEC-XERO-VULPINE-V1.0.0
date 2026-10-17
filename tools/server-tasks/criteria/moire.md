# Criteria — moire

| | |
|---|---|
| app | `moire` |
| matrix layer | 380 |
| icon group | L3 |
| wave spec | `wave32/380-moire.md` |

## Purpose

Adapts any app's look, language and input to each person: simple mode (`ui.get`, `ui.set`), languages (`xlate.languages`, `xlate.detect`, `xlate.translate`), preferences in the Hoard (`files.read`, `files.write`). Display filters and input methods have no method (as Tint and Glyphwright): use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `fs.read`
- `fs.write`
- `ai.ask`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | licence records |
| FORTRAN | trebago | contrast maths |
| Ada | eve | readability minimums |
| APL | kron | layout arrays |
| MUMPS | myra | preference tree |
| Raku | - | parsing |
| Ruby | ruby | scripting |
| Mojo | - | AI |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id moire
matrix 380        # the graph's slot IS the matrix layer
name Moire
needs sys.read
needs fs.read
needs fs.write
needs ai.ask
provides <entry> num|truth <arg>[,<arg>...]
logic <lang> <entry> <<
<source>
.
check <entry> <arg>=<int>[,<int>...] = <answer>[,<answer>...]
```

Rules the loader enforces (a violation refuses the manifest):

- Every `provides` entry must have at least one `logic` line.
- Every `logic` entry must have a `check` line with exact answers.
- The checks' inputs are integers; answers are integers, `n/d`,
  `true`, `false`, `neither`, or the word `both` (two languages
  disagreeing).
- A truth entry's checks use the four-valued answers, never 0/1.
- Inline sources end with a line containing only `.`.

