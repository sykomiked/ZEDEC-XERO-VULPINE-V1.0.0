# Criteria — calliope

| | |
|---|---|
| app | `calliope` |
| matrix layer | 374 |
| icon group | L3 |
| wave spec | `wave31/374-calliope.md` |

## Purpose

Children create illustrated stories and games with a gentle AI helper in a safe, parent-guided space: stories in the Hoard (`files.read`, `files.write`), helper suggestions from `swarm.ask` (held for the child and parent), time from `screen.check` and `screen.status`, the family's seats (`seats.list`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `ai.ask`
- `screen.time`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | allowance credits |
| FORTRAN | trebago | animation timing |
| Ada | eve | screen-time limits |
| APL | kron | story grid |
| MUMPS | myra | family tree |
| Python | python | AI |
| Pascal | pascal | step-by-step teaching |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id calliope
matrix 374        # the graph's slot IS the matrix layer
name Calliope
needs fs.read
needs fs.write
needs ai.ask
needs screen.time
needs sys.read
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

