# Criteria — wordhoard

| | |
|---|---|
| app | `wordhoard` |
| matrix layer | 46 |
| icon group | L1 |
| wave spec | `wave6/046-wordhoard.md` |

## Purpose

word lists kept in the Hoard (`files.read`, `search.query`), the language of a word (`xlate.detect`, `xlate.languages`); a definition from the model (`swarm.ask`) is held until the owner accepts it.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `search`
- `ai.ask`

## Entries and languages (from the spec's Logic line)

| the entry's job (from the spec) | languages the spec names |
|---|---|
| headword index and prefix lookup in myra | myra |
| "did you mean" edit distance over code points in C++ wasm | c, wasm |
| synonym order by shared senses in kron | kron |

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| MUMPS | myra | records |
| APL | kron | arrays |
| AWK | - | text |
| TeX | - | typesetting |
| PostScript | - | page layout |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id wordhoard
matrix 46        # the graph's slot IS the matrix layer
name Wordhoard
needs fs.read
needs search
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

