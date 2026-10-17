# cubby — the complete brief

| | |
|---|---|
| matrix layer | 103 |
| icon group | L1 |
| wave spec | `wave9/103-cubby.md` |

## Purpose

a child's seat with allowed apps and time (`screen.check`, `screen.status`, `screen.set_limit`, `screen.lock`, `seats.list`).

## Capabilities (the exact registry names for `needs`)

- `screen.time`
- `sys.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| Pascal | `pascal` | stepwise | step-by-step teaching | primary | runs |
| Rust | `rust` | safety | safety rules | primary | runs (compiled to wasm) |
| Ada | `eve` | safety | safety rules | witness | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **pascal** (Pascal): see docs/LANGUAGE_CATALOGUE.md for this tier
- **rust** (Rust): the host tier: safe systems code, exact integers, no floats; witnesses alongside the pillar implementation.
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.

## The spec's Logic clauses (what each component computes)

- "may this child open this now" (on the allowed list, minutes left, within hours, not locked) in Rust wasm + eve + kron witnessed (three witnesses)
- minutes left in kron
- activity in myra
- No contact with strangers: public spaces and social stay closed
- limit changes need the parent's passphrase

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## The manifest contract

```
zxapp 1
id cubby
matrix 103
name Cubby
needs screen.time
needs sys.read
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

