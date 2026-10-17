# collier — the complete manifest (the parameter document)

**Purpose.** digs through datasets and files in the Hoard (`search.query`, `files.list`, `files.read`, `files.write`) to extract, clean and label the useful records.

**Matrix layer 163 · icon group L2 · wave spec `wave13/163-collier.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id collier
matrix 163
name Collier
needs search
needs fs.read
needs fs.write

provides composite_key num high low
provides count_ok truth count cap
provides product num a b
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
logic kron product <<
0|_a*_b
.
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — source tree in MUMPS as MYRA

**The clause it implements.** source tree in MUMPS as MYRA

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

