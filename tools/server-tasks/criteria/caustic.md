# Criteria — caustic

| | |
|---|---|
| app | `caustic` |
| matrix layer | 329 |
| icon group | L3 |
| wave spec | `wave27/329-caustic.md` |

## Purpose

surveys and restores coral reefs: dive logs, bleaching records, water quality and restoration plots (`files.read`, `files.write`, `search.query`, `place.list`, `ledger.entries`). zxvd has no method for water temperature and quality readings: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `search`
- `location`
- `ledger.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | grant accounts |
| FORTRAN | trebago | temperature stress model |
| Ada | eve | dive safety limits |
| APL | kron | reef plot grid |
| MUMPS | myra | species tree |
| Elixir | - | fault handling |
| Logo | logo | geometry |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id caustic
matrix 329        # the graph's slot IS the matrix layer
name Caustic
needs fs.read
needs fs.write
needs search
needs location
needs ledger.read
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

