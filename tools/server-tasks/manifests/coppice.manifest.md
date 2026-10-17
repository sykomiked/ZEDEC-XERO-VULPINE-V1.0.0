# coppice — the complete manifest (the parameter document)

**Purpose.** manages woodland: tree inventory, growth, felling plans and carbon held, with places (`place.list`, `place.add`) and records in the Hoard (`files.read`, `files.write`).

**Matrix layer 192 · icon group L2 · wave spec `wave15/192-coppice.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id coppice
matrix 192
name Coppice
needs location
needs fs.read
needs fs.write
needs ledger.read

provides within_range truth value lo hi
provides count_ok truth count cap
logic eve within_range <<
eve app
values = 0 .. 100

within_range {
  value: values 0 .. 100
}
.
logic eve count_ok <<
eve app
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
}
.
check within_range value=0,50,100,101 lo=0,0,0,0 hi=100,100,100,100 = true,true,true,false
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — diameter growth per year in FORTRAN as Trebago + APL as KRON witnessed

**The clause it implements.** diameter growth per year in FORTRAN as Trebago + APL as KRON witnessed

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

### Entry 2 — stand grid in APL as KRON

**The clause it implements.** stand grid in APL as KRON

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

### Entry 3 — tree inventory in MUMPS as MYRA

**The clause it implements.** tree inventory in MUMPS as MYRA

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

