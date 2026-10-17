# Criteria — wisp

| | |
|---|---|
| app | `wisp` |
| matrix layer | 217 |
| icon group | L2 |
| wave spec | `wave18/217-wisp.md` |

## Purpose

Keeps light, offline maps, contacts and battery triage ready when power or network fails: battery, draw and source from `power.status` and `sys.info`; network state from `sys.net`; where the machine is and the saved places (`place.here`, `place.list`); emergency contacts and the medical ID from Watchfire's records (`files.read`); a low-power start with every optional switch off (`features.get`, `features.set`, owner-approved); ring a contact only on the person's request (`call.start`, signalling only). Offline map tiles and outside outage reports have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `location`
- `peers`
- `fs.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | supply list |
| FORTRAN | trebago | battery runtime model |
| Ada | eve | power reserve limits |
| APL | kron | priority grid |
| MUMPS | myra | household medical tree |
| Elixir | - | messaging |
| C# | dotnet | domain objects |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id wisp
matrix 217        # the graph's slot IS the matrix layer
name Wisp
needs sys.read
needs location
needs peers
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

