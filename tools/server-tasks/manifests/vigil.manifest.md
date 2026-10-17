# vigil — the complete manifest (the parameter document)

**Purpose.** watches servers, networks and agents through the night (`sys.info`, `sys.net`, `log.tail`, `journal.query`), waking the right person only when something truly needs a human (`notes.list`). zxvd has no method for readings from other machines (as Headland reports): use only existing methods; list what's missing in your report.

**Matrix layer 274 · icon group L3 · wave spec `wave23/274-vigil.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id vigil
matrix 274
name Vigil
needs sys.read
needs journal
needs net
needs notify
needs peers

provides ratio_x1000 num num den
logic kron ratio_x1000 <<
0|_num*1000%_den
.
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — `page_now` (Headland's alarm TRUE AND not a soft watch AND the on-call person's shift now) in Ada as Eve + APL as KRON witnessed

**The clause it implements.** `page_now` (Headland's alarm TRUE AND not a soft watch AND the on-call person's shift now) in Ada as Eve + APL as KRON witnessed

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

### Entry 2 — `load_forecast` (next hour's load from the integer trend) in FORTRAN as Trebago + APL as KRON witnessed

**The clause it implements.** `load_forecast` (next hour's load from the integer trend) in FORTRAN as Trebago + APL as KRON witnessed

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

### Entry 3 — `metrics` arrays in APL as KRON

**The clause it implements.** `metrics` arrays in APL as KRON

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

### Entry 4 — `systems` hierarchy in MUMPS as MYRA

**The clause it implements.** `systems` hierarchy in MUMPS as MYRA

**Runtime.** `eve` — the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.

**The provides line (copy exactly, choose the entry name):**

```
provides <entry_name> num|truth <arg> <arg> ...
```

**The body (this shape, this runtime):**

```
logic eve <entry_name> <<
<the source>
.
```

**The check (one line; the inputs are integers, the answers the
rows):**

```
check <entry_name> <arg>=<int>,... = <answer>,...
```

