# parley — the complete brief

| | |
|---|---|
| matrix layer | 68 |
| icon group | L1 |
| wave spec | `wave4/068-parley.md` |

## Purpose

which language (`xlate.detect`, `xlate.languages`), translate on this device (`xlate.translate`). Speech recognition and speech output have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `mic`
- `ai.ask`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| Ada | `eve` | limits | limits | primary | runs |
| Mojo | `-` | ai | AI | primary | planned: needs Lingua front-end |
| Go | `go` | concurrency | concurrency | primary | planned: SDK host only, no in-app runtime yet |
| OCaml | `-` | parse | parsing | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **-** (Mojo): see docs/LANGUAGE_CATALOGUE.md for this tier
- **go** (Go): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- text sent (valid UTF-8, code points counted, within limit) in Rust wasm + kron witnessed
- detection threshold and length limits in eve
- language pair record in myra
- Translation is advisory: clinical, legal or safety instructions need a human check

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id parley
matrix 68
name Parley
needs mic
needs ai.ask
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

