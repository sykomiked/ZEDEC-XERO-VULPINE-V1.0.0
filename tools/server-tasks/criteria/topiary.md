# Criteria — topiary

| | |
|---|---|
| app | `topiary` |
| matrix layer | 204 |
| icon group | L2 |
| wave spec | `wave16/204-topiary.md` |

## Purpose

shapes habits over months with small daily tasks and reminders in the Hoard (`files.read`, `files.write`) and the companion's nudges (`companion.mood`, `swarm.ask`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `notify`
- `ai.ask`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | habit streaks |
| FORTRAN | trebago | progress trend |
| Ada | eve | rest limits |
| APL | kron | day grid |
| MUMPS | myra | goal tree |
| IPL | - | lists |
| Mojo | - | AI |
| Logo | logo | geometry |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id topiary
matrix 204        # the graph's slot IS the matrix layer
name Topiary
needs fs.read
needs fs.write
needs notify
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

