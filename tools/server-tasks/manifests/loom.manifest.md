# loom — the complete manifest (the parameter document)

**Purpose.** Fronts app composition (kernel/src/apps/zxapp.c: manifests, `use`, `provides`, at most 8 `use` lines (ZXAPP_USES); kernel/src/modbind; kernel/src/immigration; docs/CAGE.md). Boards saved in the Hoard (`files.read`, `files.write`), test runs on scratch state (`play.run`), a composed app stored and published by CID (`aipi.put`, `aipi.publish`, `web4.cid`), its functions tried in the sandbox (`aipi.execute`). A Cage run and a manifest-check method have no method: use only existing methods; list what's missing in your report.

**Matrix layer 265 · icon group L2 · wave spec `wave22/265-loom.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id loom
matrix 265
name Loom
needs fs.read
needs fs.write
needs swarm
needs notify

provides count_ok truth count cap
provides limit_excess num value limit
provides running_state truth state event
logic eve count_ok <<
eve app
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
}
.
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — - board grid (blocks x places) in APL as KRON

**The clause it implements.** - board grid (blocks x places) in APL as KRON

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

### Entry 2 — - block tree (block, version, CID, wires) in MUMPS as MYRA

**The clause it implements.** - block tree (block, version, CID, wires) in MUMPS as MYRA

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

### Entry 3 — Paraconsistent (BOTH test, in test_zxapp.c): two blocks give different answers on the same wire, giving BOTH

**The clause it implements.** Paraconsistent (BOTH test, in test_zxapp.c): two blocks give different answers on the same wire, giving BOTH

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

