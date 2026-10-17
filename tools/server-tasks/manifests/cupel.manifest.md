# cupel — the complete manifest (the parameter document)

**Purpose.** simulates atoms, molecules and physical systems on the integer engine and compares models, runs kept in the Hoard (`files.read`, `files.write`), machine facts from `sys.info`.

**Matrix layer 179 · icon group L2 · wave spec `wave14/179-cupel.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id cupel
matrix 179
name Cupel
needs fs.read
needs fs.write
needs sys.read

provides count_ok truth count cap
provides clamp num value lo hi
provides composite_key num high low
logic eve count_ok <<
eve app
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
}
.
logic kron clamp <<
0|max(_lo, min(_hi, _value))
.
logic kron composite_key <<
0|_high*1000+_low
.
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check clamp value=-5,0,50,150 lo=0,0,0,0 hi=100,100,100,100 = 0,0,50,100
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — one integer leapfrog step (fixed-point positions and velocities, forces from a pair table) in FORTRAN as Trebago + APL as KRON witnessed

**The clause it implements.** one integer leapfrog step (fixed-point positions and velocities, forces from a pair table) in FORTRAN as Trebago + APL as KRON witnessed

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

### Entry 2 — lattice arrays in APL as KRON

**The clause it implements.** lattice arrays in APL as KRON

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

