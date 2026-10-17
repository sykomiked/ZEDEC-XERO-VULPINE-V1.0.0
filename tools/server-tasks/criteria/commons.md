# Criteria — commons

| | |
|---|---|
| app | `commons` |
| matrix layer | 27 |
| icon group | L1 |
| wave spec | `wave2/027-commons.md` |

## Purpose

rooms on this node (`chat.post`, `chat.feed`, `chat.thread`, `chat.react`, `chat.follow`, `chat.me`), groups (`social.groups`, `social.group_create`, `social.join`, `social.leave`, `social.post`), rooms across nodes (`pspace.create`, `pspace.join`, `pspace.post`, `pspace.events`, `pspace.members`) and the IRC bridge (`irc.status`, `irc.bridge`, `irc.record`). Direct messages have no method of their own: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `peers`
- `net`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| Ada | eve | limits |
| MUMPS | myra | records |
| Erlang | - | messaging |
| Icon | - | text |
| Java | java | domain objects |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id commons
matrix 27        # the graph's slot IS the matrix layer
name Commons
needs peers
needs net
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

