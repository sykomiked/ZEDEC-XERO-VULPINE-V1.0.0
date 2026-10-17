# Criteria — rondo

| | |
|---|---|
| app | `rondo` |
| matrix layer | 370 |
| icon group | L3 |
| wave spec | `wave31/370-rondo.md` |

## Purpose

Tracks personal cycles and rhythms privately, with predictions, symptoms and shared care notes, all in the Hoard (`files.read`, `files.write`); quiet hours (`screen.status`, `notes.list`); sharing only with people the person names.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `screen.time`
- `sys.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | clinic billing |
| FORTRAN | trebago | cycle prediction |
| Ada | eve | privacy limits |
| APL | kron | day grid |
| MUMPS | myra | health tree |
| Erlang | - | messaging |
| Mojo | - | AI |
| Perl | - | text |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id rondo
matrix 370        # the graph's slot IS the matrix layer
name Rondo
needs fs.read
needs fs.write
needs screen.time
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

