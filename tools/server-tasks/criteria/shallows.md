# Criteria — shallows

| | |
|---|---|
| app | `shallows` |
| matrix layer | 241 |
| icon group | L2 |
| wave spec | `wave20/241-shallows.md` |

## Purpose

Translates a live conversation between two people with captions on both sides: language and translation (`xlate.detect`, `xlate.languages`, `xlate.translate`), the call (`call.start`, `call.status`, `call.signal`), the glossary in the Hoard (`files.read`). Speech recognition and speech output have no method (as Parley): use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `mic`
- `peers`
- `ai.ask`
- `fs.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | glossary records |
| FORTRAN | trebago | confidence scoring |
| Ada | eve | latency limits |
| APL | kron | phrase matrix |
| MUMPS | myra | language tree |
| Mojo | - | AI |
| Raku | - | parsing |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id shallows
matrix 241        # the graph's slot IS the matrix layer
name Shallows
needs mic
needs peers
needs ai.ask
needs fs.read
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

