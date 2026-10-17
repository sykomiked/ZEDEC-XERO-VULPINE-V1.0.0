# Criteria — margins

| | |
|---|---|
| app | `margins` |
| matrix layer | 38 |
| icon group | L1 |
| wave spec | `wave6/038-margins.md` |

## Purpose

notes as files in the Hoard's Notes folder (`files.list`, `files.read`, `files.write`, `files.rename`, `files.trash`), found with `search.query`. Build on kernel/src/apps/notes_app.c.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `search`

## Entries and languages (from the spec's Logic line)

| the entry's job (from the spec) | languages the spec names |
|---|---|
| note size limits in eve | eve |
| tag index in myra | myra |
| pinned and recent order in kron | kron |

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| MUMPS | myra | records |
| APL | kron | arrays |
| SNOBOL | - | text |
| TeX | - | typesetting |
| PostScript | - | page layout |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id margins
matrix 38        # the graph's slot IS the matrix layer
name Margins
needs fs.read
needs fs.write
needs search
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

