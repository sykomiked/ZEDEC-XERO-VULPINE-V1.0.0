# Criteria — belvedere

| | |
|---|---|
| app | `belvedere` |
| matrix layer | 87 |
| icon group | L1 |
| wave spec | `wave8/087-belvedere.md` |

## Purpose

the public face of your space (`spaces.create`, `spaces.get`, `spaces.set_cid`, `pspace.create`, `files.cross`, `web4.cid`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `peers`
- `net`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| MUMPS | myra | records |
| FORTRAN | trebago | exact numerics |
| TypeScript | typescript | view and model |
| Logo | logo | geometry |
| Java | java | domain objects |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id belvedere
matrix 87        # the graph's slot IS the matrix layer
name Belvedere
needs fs.read
needs fs.write
needs peers
needs net
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

