# rill — the complete manifest (the parameter document)

**Purpose.** gives children a safe reading space with read-aloud, word help and story choices, from the Hoard (`files.read`) within their screen time (`screen.check`, `screen.status`). zxvd has no method for text-to-speech: use only existing methods; list what's missing in your report.

**Matrix layer 216 · icon group L2 · wave spec `wave17/216-rill.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id rill
matrix 216
name Rill
needs fs.read
needs screen.time
needs sys.read

provides count_ok truth count cap
provides grade_band num value b1 b2 b3 b4
logic eve count_ok <<
eve app
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
}
.
logic kron grade_band <<
(_value>_b1)+(_value>_b2)+(_value>_b3)+(_value>_b4)
.
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — word help steps (sound it out, meaning, example) in BASIC (twin Hatchling)

**The clause it implements.** word help steps (sound it out, meaning, example) in BASIC (twin Hatchling)

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

### Entry 2 — vocabulary grid (child by word) in APL as KRON

**The clause it implements.** vocabulary grid (child by word) in APL as KRON

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

### Entry 3 — child profile tree in MUMPS as MYRA

**The clause it implements.** child profile tree in MUMPS as MYRA

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

