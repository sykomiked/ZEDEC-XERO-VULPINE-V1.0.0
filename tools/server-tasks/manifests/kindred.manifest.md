# kindred — the complete manifest (the parameter document)

**Purpose.** contacts as records kept in the Hoard (`files.read`, `files.write`), a contact's agent card checked with `web4.card_verify`, their node from `net.peers`, chat identity from `chat.me`.

**Matrix layer 44 · icon group L1 · wave spec `wave6/044-kindred.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id kindred
matrix 44
name Kindred
needs fs.read
needs fs.write
needs peers

provides composite_key num high low
provides limit_excess num value limit
logic kron composite_key <<
0|_high*1000+_low
.
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — likely duplicates (same card id or same name codes) in kron

**The clause it implements.** likely duplicates (same card id or same name codes) in kron

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

