# cutwater — the complete manifest (the parameter document)

**Purpose.** sources from the Hoard (`files.read`), cut points checked with `stream.plan`, the edit list saved (`files.write`). Rendering a finished video has no method: use only existing methods; list what's missing in your report.

**Matrix layer 56 · icon group L1 · wave spec `wave7/056-cutwater.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id cutwater
matrix 56
name Cutwater
needs fs.read
needs fs.write

provides ratio_x1000 num num den
provides composite_key num high low
logic kron ratio_x1000 <<
0|_num*1000%_den
.
logic kron composite_key <<
0|_high*1000+_low
.
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — edit list valid (in before out, inside the source, no overlap on a track) in eve + kron witnessed

**The clause it implements.** edit list valid (in before out, inside the source, no overlap on a track) in eve + kron witnessed

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

