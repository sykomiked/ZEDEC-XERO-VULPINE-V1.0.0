# meadow — the complete manifest (the parameter document)

**Purpose.** find, join and use public spaces (`pspace.find`, `pspace.join`, `pspace.leave`, `pspace.list`, `pspace.members`, `pspace.events`, `pspace.post`, `spaces.list` for park rights). Feature `pspace` is off by default.

**Matrix layer 86 · icon group L1 · wave spec `wave2/086-meadow.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id meadow
matrix 86
name Meadow
needs peers
needs net
needs notify

provides composite_key num high low
provides count_ok truth count cap
logic kron composite_key <<
0|_high*1000+_low
.
logic eve count_ok <<
eve app
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
}
.
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — Posts cannot be deleted (`pspace.post`): the app says so before sending

**The clause it implements.** Posts cannot be deleted (`pspace.post`): the app says so before sending

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

