# broadsheet — the complete manifest (the parameter document)

**Purpose.** open documents from the Hoard (`files.read`, base64), print (`print.submit`); annotations go to a separate file beside the original (`files.write`), never into it.

**Matrix layer 42 · icon group L1 · wave spec `wave6/042-broadsheet.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id broadsheet
matrix 42
name Broadsheet
needs fs.read
needs fs.write
needs print

provides composite_key num high low
logic kron composite_key <<
0|_high*1000+_low
.
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — "well formed" (header present, every cross-reference offset inside the file size) in Rust wasm + eve witnessed, so a malformed file is refused

**The clause it implements.** "well formed" (header present, every cross-reference offset inside the file size) in Rust wasm + eve witnessed, so a malformed file is refused

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

### Entry 2 — zoom to fit page in kron

**The clause it implements.** zoom to fit page in kron

**Runtime.** `myra` — the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.

**The provides line (copy exactly, choose the entry name):**

```
provides <entry_name> num|truth <arg> <arg> ...
```

**The body (this shape, this runtime):**

```
logic myra <entry_name> <<
<the source>
.
```

**The check (one line; the inputs are integers, the answers the
rows):**

```
check <entry_name> <arg>=<int>,... = <answer>,...
```

