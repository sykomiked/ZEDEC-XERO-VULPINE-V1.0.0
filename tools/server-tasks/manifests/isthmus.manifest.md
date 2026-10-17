# isthmus — the complete manifest (the parameter document)

**Purpose.** Mediates disputes: the case room (`pspace.create`, `pspace.post`, `chat.post`, `chat.thread`), the record (`journal.query`, `files.read`, `files.write`), signed copies checked (`web4.verify`), printed agreements (`print.submit`).

**Matrix layer 240 · icon group L2 · wave spec `wave19/240-isthmus.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id isthmus
matrix 240
name Isthmus
needs peers
needs net
needs journal
needs fs.read
needs fs.write
needs print
needs notify

provides share_x100 num part whole
provides largest_remainder num pool w1 w2 w3 w4
provides product num a b
provides composite_key num high low
provides count_ok truth count cap
provides running_state truth state event
logic kron share_x100 <<
0|_part*10000%_whole
.
logic kron largest_remainder <<
0|_pool*_w1%(_w1+_w2+_w3+_w4)
.
logic kron product <<
0|_a*_b
.
logic kron composite_key <<
0|_high*1000+_low
.
logic eve count_ok <<
eve app
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
}
.
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000
check largest_remainder pool=100,10,7,100 w1=1,1,1,1 w2=1,1,1,1 w3=1,1,1,1 w4=1,0,0,0 = 25,4,3,34
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — - (critical) "next step allowed" (both parties heard, cooling-off over, both consented to the proposal) in Ada as Eve + APL as KRON witnessed

**The clause it implements.** - (critical) "next step allowed" (both parties heard, cooling-off over, both consented to the proposal) in Ada as Eve + APL as KRON witnessed

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

### Entry 2 — - claim matrix (claims x parties: agreed, disputed) in APL as KRON

**The clause it implements.** - claim matrix (claims x parties: agreed, disputed) in APL as KRON

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

### Entry 3 — - party tree in MUMPS as MYRA

**The clause it implements.** - party tree in MUMPS as MYRA

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

### Entry 4 — Paraconsistent (BOTH test, in test_zxapp.c): party A says the goods were delivered on 3 May and party B says they were not, giving BOTH

**The clause it implements.** Paraconsistent (BOTH test, in test_zxapp.c): party A says the goods were delivered on 3 May and party B says they were not, giving BOTH

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

### Entry 5 — never re-derive the tables

**The clause it implements.** never re-derive the tables

**Runtime.** `kron` — the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.

**The provides line (copy exactly, choose the entry name):**

```
provides <entry_name> num|truth <arg> <arg> ...
```

**The body (this shape, this runtime):**

```
logic kron <entry_name> <<
<the source>
.
```

**The check (one line; the inputs are integers, the answers the
rows):**

```
check <entry_name> <arg>=<int>,... = <answer>,...
```

