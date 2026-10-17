# Criteria — sealwax

| | |
|---|---|
| app | `sealwax` |
| matrix layer | 47 |
| icon group | L1 |
| wave spec | `wave6/047-sealwax.md` |

## Purpose

forms as files (`files.read`, `files.write`), printed (`print.submit`), a signed copy checked with `web4.verify`. Signing a document with the owner's key has no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `print`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| MUMPS | myra | records |
| APL | kron | arrays |
| Icon | - | text |
| PostScript | - | page layout |
| TeX | - | typesetting |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id sealwax
matrix 47        # the graph's slot IS the matrix layer
name Sealwax
needs fs.read
needs fs.write
needs print
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

