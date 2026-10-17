# Criteria — hark

| | |
|---|---|
| app | `hark` |
| matrix layer | 134 |
| icon group | L1 |
| wave spec | `wave11/134-hark.md` |

## Purpose

run commands by voice; free requests go to `swarm.ask` and come back as suggestions held for the person; language from `xlate.detect`. zxvd has no speech-to-text method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `mic`
- `ai.ask`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| Ada | eve | safety rules |
| MUMPS | myra | records |
| Mojo | - | AI |
| Racket | - | parsing |
| Perl | - | text |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id hark
matrix 134        # the graph's slot IS the matrix layer
name Hark
needs mic
needs ai.ask
needs notify
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

