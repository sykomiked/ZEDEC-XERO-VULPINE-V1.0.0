# Criteria — nightjar

| | |
|---|---|
| app | `nightjar` |
| matrix layer | 133 |
| icon group | L1 |
| wave spec | `wave4/133-nightjar.md` |

## Purpose

speak what is on screen and where focus is; language of text from `xlate.detect`, simple mode from `ui.get`. zxvd has no accessibility-tree or speech method (kernel/src/speech is not exposed): use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| Ada | eve | safety rules |
| MUMPS | myra | records |
| OCaml | - | parsing |
| HyperTalk | - | view and model |
| Python | python | AI |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id nightjar
matrix 133        # the graph's slot IS the matrix layer
name Nightjar
needs sys.read
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

