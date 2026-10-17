# lifeboat — the complete brief

| | |
|---|---|
| matrix layer | 11 |
| icon group | L1 |
| wave spec | `wave4/011-lifeboat.md` |

## Purpose

start safe (every switch off: `features.get`, `features.set`), check (`journal.verify`, `ledger.check`, `sys.disk`, `backup.verify`), restore from Keepsake (`backup.list`, `backup.restore`), check for a signed reinstall (`update.check` only). Bare-metal reinstall and boot-time repair of the Hoard have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `fs.read`
- `fs.write`
- `backup`
- `journal`
- `ledger.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| Ada | `eve` | safety | safety rules | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| Elixir | `-` | faults | fault handling | primary | planned: needs Lingua front-end |
| Crystal | `-` | machine | low-level system | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Elixir): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "safe to start normally" from the check results (journal intact, books balanced, free bytes above floor, backup tested, each 0/1 or a count) in forth + eve witnessed
- next repair step from those results in basic + kron witnessed
- "restore fits" (bytes needed vs free) in sutra + kron witnessed
- recovery record in myra
- Nothing is restored, switched or reinstalled without the owner's approval
- restore always makes a new item. Tests use a scratch Hoard and books, never the real ones

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id lifeboat
matrix 11
name Lifeboat
needs sys.read
needs fs.read
needs fs.write
needs backup
needs journal
needs ledger.read
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

