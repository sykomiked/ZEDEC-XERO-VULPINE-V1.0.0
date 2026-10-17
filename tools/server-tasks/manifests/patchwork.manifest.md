# patchwork — the complete manifest (the parameter document)

**Purpose.** Gathers a family's history: stories, photos, records and voices in the Hoard (`files.read`, `files.write`, `search.query`), versions (`backup.versions`), the family space (`spaces.get`, `spaces.set_members`), published to the family by CID (`aipi.put`, `web4.cid`).

**Matrix layer 388 · icon group L3 · wave spec `wave32/388-patchwork.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id patchwork
matrix 388
name Patchwork
needs fs.read
needs fs.write
needs search
needs backup
needs peers
needs net
needs ai.ask

provides limit_excess num value limit
provides share_x100 num part whole
provides clamp num value lo hi
provides product num a b
provides count_ok truth count cap
provides running_state truth state event
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic kron share_x100 <<
0|_part*10000%_whole
.
logic kron clamp <<
0|max(_lo, min(_hi, _value))
.
logic kron product <<
0|_a*_b
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
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000
check clamp value=-5,0,50,150 lo=0,0,0,0 hi=100,100,100,100 = 0,0,50,100
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — others by the family's rule) in Ada as Eve + APL as KRON witnessed

**The clause it implements.** others by the family's rule) in Ada as Eve + APL as KRON witnessed

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

### Entry 2 — - `timeline`: timeline grid in APL as KRON

**The clause it implements.** - `timeline`: timeline grid in APL as KRON

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

### Entry 3 — - `family`: family tree in MUMPS as MYRA

**The clause it implements.** - `family`: family tree in MUMPS as MYRA

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

### Entry 4 — Paraconsistent (BOTH test, in test_zxapp.c): two relatives give different years for a wedding: the date is BOTH

**The clause it implements.** Paraconsistent (BOTH test, in test_zxapp.c): two relatives give different years for a wedding: the date is BOTH

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

