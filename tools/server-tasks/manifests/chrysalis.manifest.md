# chrysalis — the complete manifest (the parameter document)

**Purpose.** Fronts guest machines (kernel/src/hwemu with hwe.c and hwa.c, kernel/src/virtio, kernel/src/legacy, kernel/src/capctl; no hypervisor exists yet). The host (`sys.info`, `sys.disk`), energy and budgets (`power.top`, `power.status`, `power.budget`), switches (`features.get`), guest disk images in the Hoard (`files.read`, `files.write`). Creating, starting and pausing a guest, and the driver machine, have no method: use only existing methods; list what's missing in your report.

**Matrix layer 268 · icon group L2 · wave spec `wave22/268-chrysalis.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id chrysalis
matrix 268
name Chrysalis
needs sys.read
needs fs.read
needs fs.write
needs notify

provides share_x100 num part whole
provides count_ok truth count cap
provides composite_key num high low
provides running_state truth state event
logic kron share_x100 <<
0|_part*10000%_whole
.
logic eve count_ok <<
eve app
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
}
.
logic kron composite_key <<
0|_high*1000+_low
.
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — wasm owns sandboxing here)

**The clause it implements.** wasm owns sandboxing here)

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

### Entry 2 — - guest table (guests x CPU, memory, devices) in APL as KRON

**The clause it implements.** - guest table (guests x CPU, memory, devices) in APL as KRON

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

### Entry 3 — Paraconsistent (BOTH test, in test_zxapp.c): a guest reports itself idle while the host measures it at 100% CPU, giving BOTH

**The clause it implements.** Paraconsistent (BOTH test, in test_zxapp.c): a guest reports itself idle while the host measures it at 100% CPU, giving BOTH

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

