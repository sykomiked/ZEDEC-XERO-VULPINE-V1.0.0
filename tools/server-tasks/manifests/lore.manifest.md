# lore — the complete manifest (the parameter document)

**Purpose.** the owner's notes and files (`files.list`, `files.read`, `search.query`, `search.refresh`), read by the companion (`swarm.ask`) only with leave.

**Matrix layer 67 · icon group L1 · wave spec `wave7/067-lore.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id lore
matrix 67
name Lore
needs fs.read
needs search
needs ai.ask

provides difference num a b
provides composite_key num high low
logic kron difference <<
0|_a-_b
.
logic kron composite_key <<
0|_high*1000+_low
.
check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — "may the AI read this file" (leave given, folder granted, not from a more private space) in eve + kron witnessed

**The clause it implements.** "may the AI read this file" (leave given, folder granted, not from a more private space) in eve + kron witnessed

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

