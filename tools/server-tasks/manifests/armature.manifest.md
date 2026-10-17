# armature — the complete manifest (the parameter document)

**Purpose.** an invention lab notebook: experiments, prototypes, parts, results and dated proof of who invented what (`files.read`, `files.write`, `journal.query`, `web4.cid`, `aipi.put`).

**Matrix layer 297 · icon group L3 · wave spec `wave25/297-armature.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id armature
matrix 297
name Armature
needs fs.read
needs fs.write
needs journal
needs sys.read

provides ratio_x1000 num num den
provides product num a b
logic kron ratio_x1000 <<
0|_num*1000%_den
.
logic kron product <<
0|_a*_b
.
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — `proof_ok` (an entry is dated proof only when its content id is on the journal and the chain is intact) in Ada as Eve + APL as KRON witnessed

**The clause it implements.** `proof_ok` (an entry is dated proof only when its content id is on the journal and the chain is intact) in Ada as Eve + APL as KRON witnessed

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

### Entry 2 — `results` matrix in APL as KRON

**The clause it implements.** `results` matrix in APL as KRON

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

### Entry 3 — `revisions` tree in MUMPS as MYRA

**The clause it implements.** `revisions` tree in MUMPS as MYRA

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

