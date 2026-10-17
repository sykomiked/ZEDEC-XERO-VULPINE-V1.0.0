# elevation — the complete manifest (the parameter document)

**Purpose.** designs buildings from sketch to construction drawings, with structure checks, materials and cost (`files.read`, `files.write`, `print.submit`).

**Matrix layer 301 · icon group L3 · wave spec `wave25/301-elevation.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id elevation
matrix 301
name Elevation
needs fs.read
needs fs.write
needs print
needs notify

provides product num a b
provides count_ok truth count cap
provides modular_draw num seed members step
logic kron product <<
0|_a*_b
.
logic eve count_ok <<
eve app
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
}
.
logic kron modular_draw <<
0|(_seed*1103515245+12345+_step*7)%_members
.
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — `structure` (member forces and deflection, integer) in FORTRAN as Trebago + APL as KRON witnessed

**The clause it implements.** `structure` (member forces and deflection, integer) in FORTRAN as Trebago + APL as KRON witnessed

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

### Entry 2 — `elements` tree in MUMPS as MYRA

**The clause it implements.** `elements` tree in MUMPS as MYRA

**Runtime.** `sutra` — the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.

**The provides line (copy exactly, choose the entry name):**

```
provides <entry_name> num|truth <arg> <arg> ...
```

**The body (this shape, this runtime):**

```
logic sutra <entry_name> <<
<the source>
.
```

**The check (one line; the inputs are integers, the answers the
rows):**

```
check <entry_name> <arg>=<int>,... = <answer>,...
```

