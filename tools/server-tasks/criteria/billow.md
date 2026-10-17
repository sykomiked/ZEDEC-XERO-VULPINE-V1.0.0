# Criteria — billow

| | |
|---|---|
| app | `billow` |
| matrix layer | 238 |
| icon group | L2 |
| wave spec | `wave19/238-billow.md` |

## Purpose

Broadcasts live audio shows to the mesh with captions and archives: the show plan and segments (`stream.plan`, `stream.segment`), a listening hall (`hall.load`, `hall.play`, `hall.pause`, `hall.status`), the show's space (`pspace.create`, `pspace.post`), captions translated (`xlate.translate`), the archive (`files.write`, `aipi.put`, `web4.cid`). Live audio capture has no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `mic`
- `peers`
- `net`
- `fs.read`
- `fs.write`
- `ai.ask`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | airtime and rights log |
| FORTRAN | trebago | audio levels |
| Ada | eve | loudness limits |
| APL | kron | schedule grid |
| MUMPS | myra | episode tree |
| Go | go | messaging |
| Python | python | AI |
| AWK | - | text |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id billow
matrix 238        # the graph's slot IS the matrix layer
name Billow
needs mic
needs peers
needs net
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

