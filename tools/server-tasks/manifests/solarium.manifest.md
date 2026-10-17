# solarium — the complete manifest (the parameter document)

**Purpose.** looks after a garden or greenhouse: planting plans, watering, soil and light sensors, and harvest logs (`files.read`, `files.write`, `place.here`). zxvd has no method for soil and light sensor readings, valve control: use only existing methods; list what's missing in your report.

**Matrix layer 292 · icon group L3 · wave spec `wave24/292-solarium.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id solarium
matrix 292
name Solarium
needs fs.read
needs fs.write
needs location
needs net
needs notify

provides ratio_x1000 num num den
provides modular_draw num seed members step
provides count_ok truth count cap
logic kron ratio_x1000 <<
0|_num*1000%_den
.
logic kron modular_draw <<
0|(_seed*1103515245+12345+_step*7)%_members
.
logic eve count_ok <<
eve app
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
}
.
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750
check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — `water_ok` (Dibber's "may water now" AND soil below the declared moisture AND no rain in the forecast window) in Ada as Eve + APL as KRON witnessed

**The clause it implements.** `water_ok` (Dibber's "may water now" AND soil below the declared moisture AND no rain in the forecast window) in Ada as Eve + APL as KRON witnessed

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

### Entry 2 — `beds` sensor grid in APL as KRON

**The clause it implements.** `beds` sensor grid in APL as KRON

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

### Entry 3 — `lineage` (plant lineage) in MUMPS as MYRA

**The clause it implements.** `lineage` (plant lineage) in MUMPS as MYRA

**Runtime.** `trebago` — the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).

**The provides line (copy exactly, choose the entry name):**

```
provides <entry_name> num|truth <arg> <arg> ...
```

**The body (this shape, this runtime):**

```
logic trebago <entry_name> <<
<the source>
.
```

**The check (one line; the inputs are integers, the answers the
rows):**

```
check <entry_name> <arg>=<int>,... = <answer>,...
```

