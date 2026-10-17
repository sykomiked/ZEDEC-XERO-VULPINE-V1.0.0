# bonfire — the complete manifest (the parameter document)

**Purpose.** events in a public space or group (`pspace.create`, `pspace.post`, `pspace.members`, `social.group_create`, `place.list`). No calendar or RSVP method: use only existing methods; list what's missing in your report.

**Matrix layer 95 · icon group L1 · wave spec `wave8/095-bonfire.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id bonfire
matrix 95
name Bonfire
needs peers
needs net
needs location
needs notify

provides difference num a b
provides count_ok truth count cap
logic kron difference <<
0|_a-_b
.
logic eve count_ok <<
eve app
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
}
.
check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — events and invite lists in myra

**The clause it implements.** events and invite lists in myra

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

