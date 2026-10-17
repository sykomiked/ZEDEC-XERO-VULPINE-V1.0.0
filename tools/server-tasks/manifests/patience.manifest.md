# patience — the complete manifest (the parameter document)

**Purpose.** solitaire card games; saved games in the Banker (`files.read`, `files.write`).

**Matrix layer 105 · icon group L1 · wave spec `wave9/105-patience.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id patience
matrix 105
name Patience
needs fs.read
needs fs.write

provides running_state truth state event
provides modular_draw num seed members step
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
logic kron modular_draw <<
0|(_seed*1103515245+12345+_step*7)%_members
.
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — games in myra

**The clause it implements.** games in myra

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

