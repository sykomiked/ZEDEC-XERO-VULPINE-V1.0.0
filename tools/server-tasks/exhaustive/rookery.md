# rookery — the exhaustive specification

| field | value |
|---|---|
| app id | `rookery` |
| matrix layer | 26 |
| icon group | L1 |
| wave spec | `wave5/026-rookery.md` |
| status | pending |

## 1. Purpose

letters as signed envelopes to another companion (`web4.post`; held until `web4.consent` when pii is set), read from `web4.feed` and checked with `web4.verify`; drafts and folders in the Hoard (`files.write`, `files.list`). zxvd has no mail transport and no Web4 delivery yet: use only existing methods; list what's missing in your report.

## 2. Capabilities (the exact registry names and scopes)

- `needs net`
- `needs fs.read`
- `needs fs.write`
- `needs notify`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use coffer 1` — call `<id>.<entry>` after the use line
- `use lockbox 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| Ada | `eve` | limits | limits | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| Go | `go` | messaging | messaging | primary | planned: SDK host only, no in-app runtime yet |
| Delphi | `-` | view | view and model | primary | planned: needs Lingua front-end |
| Elixir | `-` | faults | fault handling | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id rookery
matrix 26
name Rookery
needs net
needs fs.read
needs fs.write
needs notify

provides limit_excess num value limit
provides composite_key num high low
provides clamp num value lo hi
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic kron composite_key <<
0|((_high*1000)+_low)
.
logic kron clamp <<
0|max(_lo, min(_hi, _value))
.
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check limit_excess value=1000,999 limit=1000,1000 = 0,0
check limit_excess value=1,1000000 limit=0,999999 = 1,1
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check clamp value=-5,0,50,150 lo=0,0,0,0 hi=100,100,100,100 = 0,0,50,100
```

## 6. The entry specifications (what each entry computes)

### `limit_excess` (num) — trebago

**Args.** `value limit`

**Semantics.** The excess over a ceiling. 0 when value <= limit, else value - limit. Integer only; the limit is inclusive; no negative excess ever.

**The exact check.** `['check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000', 'check limit_excess value=1000,999 limit=1000,1000 = 0,0', 'check limit_excess value=1,1000000 limit=0,999999 = 1,1']`

### `composite_key` (num) — kron

**Args.** `high low`

**Semantics.** The record's composite index: high*1000 + low. Explicit parens: K evaluates right to left.

**The exact check.** `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999`

### `clamp` (num) — kron

**Args.** `value lo hi`

**Semantics.** The value clamped into [lo,hi]. The result is always within the bounds.

**The exact check.** `check clamp value=-5,0,50,150 lo=0,0,0,0 hi=100,100,100,100 = 0,0,50,100`

## 7. App-specific entries

None — this app is fully covered by the verified library. Emit the manifest in section 5 exactly.

## 8. The dual interface (human and AI)

- **Human.** The app's icon (its matrix slot's ZXVG) launches it from the desk; its entries' views render on the framebuffer.
- **AI.** Every provided entry is callable through the app runtime (`zxapp_call`) and the `--ai-manifest` surface; the same state the human sees.

## 9. The test plan

- The node loads the manifest (`zxapp_load`): every logic entry compiles.
- Every check line runs and passes (`zxapp_check`): the exact answers.
- The browser engine (`zxv_app.wasm`) loads the same manifest and passes the same checks.

