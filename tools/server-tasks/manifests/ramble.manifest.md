# ramble — the complete manifest (the parameter document)

**Purpose.** the owner's envelope feed (`web4.open`, `web4.feed`, `web4.envelope`, `web4.verify`), content by CID (`aipi.get`, `web4.cid`), agent cards (`web4.card_verify`) and Chiglet intents held for consent (`web4.consent`). Pages over HTTP have no method: use only existing methods; list what's missing in your report.

**Matrix layer 25 · icon group L1 · wave spec `wave2/025-ramble.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id ramble
matrix 25
name Ramble
needs net
needs ledger.read
needs notify

provides limit_excess num value limit
provides product num a b
provides composite_key num high low
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic kron product <<
0|_a*_b
.
logic kron composite_key <<
0|_high*1000+_low
.
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — CID string check (prefix, length, base32 alphabet, as byte codes) in Rust wasm + kron witnessed

**The clause it implements.** CID string check (prefix, length, base32 alphabet, as byte codes) in Rust wasm + kron witnessed

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**The provides line (copy exactly, choose the entry name):**

```
provides <entry_name> num|truth <arg> <arg> ...
```

**The body (this shape, this runtime):**

```
logic c <entry_name> <<
<the source>
.
```

**The check (one line; the inputs are integers, the answers the
rows):**

```
check <entry_name> <arg>=<int>,... = <answer>,...
```

### Entry 2 — Ramble never grants consent by itself: a grant with money is advisory and the owner's act

**The clause it implements.** Ramble never grants consent by itself: a grant with money is advisory and the owner's act

**Runtime.** `eve` — the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.

**The provides line (copy exactly, choose the entry name):**

```
provides <entry_name> num|truth <arg> <arg> ...
```

**The body (this shape, this runtime):**

```
logic eve <entry_name> <<
<the source>
.
```

**The check (one line; the inputs are integers, the answers the
rows):**

```
check <entry_name> <arg>=<int>,... = <answer>,...
```

