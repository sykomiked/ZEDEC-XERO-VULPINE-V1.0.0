# Criteria — compositor

| | |
|---|---|
| app | `compositor` |
| matrix layer | 165 |
| icon group | L2 |
| wave spec | `wave13/165-compositor.md` |

## Purpose

writes and runs scripts in any pillar language from one workbench (`play.catalogue`, `play.run`, `play.draft`), saved in the Hoard (`files.read`, `files.write`), with debugging built in.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `sys.read`
- `ai.ask`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | job records |
| FORTRAN | trebago | numeric routines |
| Ada | eve | contracts |
| APL | kron | array REPL |
| MUMPS | myra | global trees |
| Ruby | ruby | scripting |
| Raku | - | parsing |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id compositor
matrix 165        # the graph's slot IS the matrix layer
name Compositor
needs fs.read
needs fs.write
needs sys.read
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

