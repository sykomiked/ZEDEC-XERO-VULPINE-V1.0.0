# scree — the complete manifest (the parameter document)

**Purpose.** Builds and plays logic and maths puzzles that adapt to a learner's level; puzzles and progress in the Hoard (`files.read`, `files.write`), hints from `swarm.ask` held until the learner asks, time from `screen.check`.

**Matrix layer 219 · icon group L2 · wave spec `wave18/219-scree.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id scree
matrix 219
name Scree
needs fs.read
needs fs.write
needs ai.ask
needs screen.time

provides share_x100 num part whole
provides grade_band num value b1 b2 b3 b4
provides difference num a b
provides modular_draw num seed members step
provides composite_key num high low
provides running_state truth state event
logic kron share_x100 <<
0|_part*10000%_whole
.
logic kron grade_band <<
(_value>_b1)+(_value>_b2)+(_value>_b3)+(_value>_b4)
.
logic kron difference <<
0|_a-_b
.
logic kron modular_draw <<
0|(_seed*1103515245+12345+_step*7)%_members
.
logic kron composite_key <<
0|_high*1000+_low
.
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000
check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4
check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1
check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — - the worked solution shown one step at a time in BASIC (twin Hatchling)

**The clause it implements.** - the worked solution shown one step at a time in BASIC (twin Hatchling)

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

### Entry 2 — - Forth and BASIC blocks are committed in twin form: the manifest's logic line names the twin (`logic cairndrake ...`, `logic hatchling ...`)

**The clause it implements.** - Forth and BASIC blocks are committed in twin form: the manifest's logic line names the twin (`logic cairndrake ...`, `logic hatchling ...`)

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

### Entry 3 — Paraconsistent (BOTH test, in test_zxapp.c): a 4x4 grid with two valid fillings: the stored answer differs from the learner's (FALSE) while Riddle's "solved" rule passes (TRUE), giving BOTH

**The clause it implements.** Paraconsistent (BOTH test, in test_zxapp.c): a 4x4 grid with two valid fillings: the stored answer differs from the learner's (FALSE) while Riddle's "solved" rule passes (TRUE), giving BOTH

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

