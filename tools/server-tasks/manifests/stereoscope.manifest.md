# stereoscope — the complete manifest (the parameter document)

**Purpose.** Shoots, edits and publishes photo stories with signed originals, tracked edits and licences sold per image: photos in the Hoard (`files.read`, `files.write`, `sys.disk`), published by CID (`aipi.put`, `web4.cid`), signatures checked (`web4.verify`), licence sales in the books (`ledger.post`, `ledger.entries`).

**Matrix layer 350 · icon group L3 · wave spec `wave29/350-stereoscope.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id stereoscope
matrix 350
name Stereoscope
needs camera
needs fs.read
needs fs.write
needs sys.read
needs net
needs ledger.read
needs ledger.post

provides product num a b
provides limit_excess num value limit
provides ratio_x1000 num num den
provides running_state truth state event
logic kron product <<
0|_a*_b
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
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — - `edited_mark`: (critical) "show as edited" (the edit's content id differs from the signed original's) in Ada as Eve + APL as KRON witnessed

**The clause it implements.** - `edited_mark`: (critical) "show as edited" (the edit's content id differs from the signed original's) in Ada as Eve + APL as KRON witnessed

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

### Entry 2 — - `colour`: colour science (white balance gains, integer 3x3 matrices q12) in FORTRAN as Trebago

**The clause it implements.** - `colour`: colour science (white balance gains, integer 3x3 matrices q12) in FORTRAN as Trebago

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

### Entry 3 — - `albums`: shoot album tree in MUMPS as MYRA

**The clause it implements.** - `albums`: shoot album tree in MUMPS as MYRA

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

