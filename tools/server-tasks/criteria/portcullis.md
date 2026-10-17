# Criteria — portcullis

| | |
|---|---|
| app | `portcullis` |
| matrix layer | 336 |
| icon group | L3 |
| wave spec | `wave28/336-portcullis.md` |

## Purpose

controls entry to venues and communities: invitations, tickets, guest lists and who may come in (`web4.verify`, `net.peers`, `files.read`, `files.write`).

## Capabilities (needs — the exact registry names)

- `net`
- `peers`
- `fs.read`
- `fs.write`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | ticket sales |
| FORTRAN | trebago | crowd flow model |
| Ada | eve | capacity limits |
| APL | kron | entry grid |
| MUMPS | myra | guest hierarchy |
| LISP | lisp | lists |
| Zig | - | low-level system |
| WebAssembly | wasm | sandboxing |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id portcullis
matrix 336        # the graph's slot IS the matrix layer
name Portcullis
needs net
needs peers
needs fs.read
needs fs.write
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

