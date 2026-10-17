# planisphere — the complete manifest (the parameter document)

**Purpose.** Your own computer from disc to desktop in one place, every step signed: install (Plinth), firmware and boot (Fusee), devices (Muster), first start (Overture) and community updates (Thicket). Uses only their daemon methods (`sys.disk`, `sys.info`, `update.check`, `journal.verify`, `journal.query`, `features.get`, `seats.list`, `spaces.propose`, `spaces.vote`); the steps they report as missing stay missing here: list them in your report.

**Matrix layer 391 · icon group L3 · wave spec `wave32/391-planisphere.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id planisphere
matrix 391
name Planisphere
needs sys.read
needs fs.read
needs fs.write
needs journal
needs backup
needs net
needs peers
needs notify

provides running_state truth state event
provides limit_excess num value limit
provides ratio_x1000 num num den
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic kron ratio_x1000 <<
0|_num*1000%_den
.
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — - `step_ok`: (critical) "do this step" (its part's check TRUE AND every earlier step done AND the owner confirmed

**The clause it implements.** - `step_ok`: (critical) "do this step" (its part's check TRUE AND every earlier step done AND the owner confirmed

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

### Entry 2 — - `history`: machine history (signed steps, refusing on overflow) in COBOL as Sutra

**The clause it implements.** - `history`: machine history (signed steps, refusing on overflow) in COBOL as Sutra

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

### Entry 3 — Paraconsistent (BOTH test, in test_zxapp.c): Fusee passes a boot image and Thicket's auditors disagree on the update inside it: the step is BOTH

**The clause it implements.** Paraconsistent (BOTH test, in test_zxapp.c): Fusee passes a boot image and Thicket's auditors disagree on the update inside it: the step is BOTH

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

### Entry 4 — never re-derive the tables

**The clause it implements.** never re-derive the tables

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

