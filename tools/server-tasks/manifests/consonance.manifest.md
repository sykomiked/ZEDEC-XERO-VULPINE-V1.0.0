# consonance — the complete manifest (the parameter document)

**Purpose.** Negotiates, drafts and signs agreements between groups or nations: drafts in the Hoard (`files.read`, `files.write`), revisions on the journal (`journal.query`, `journal.verify`), signatures checked (`web4.verify`), translation (`xlate.translate`, `xlate.detect`), the negotiating room (`pspace.create`, `pspace.post`).

**Matrix layer 352 · icon group L3 · wave spec `wave29/352-consonance.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id consonance
matrix 352
name Consonance
needs fs.read
needs fs.write
needs journal
needs peers
needs net
needs ai.ask
needs notify

provides share_x100 num part whole
provides largest_remainder num pool w1 w2 w3 w4
provides product num a b
provides limit_excess num value limit
provides running_state truth state event
provides flags_none truth bits forbidden
logic kron share_x100 <<
0|_part*10000%_whole
.
logic kron largest_remainder <<
0|_pool*_w1%(_w1+_w2+_w3+_w4)
.
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
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
logic kron flags_none <<
(_bits&_forbidden)=0
.
check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000
check largest_remainder pool=100,10,7,100 w1=1,1,1,1 w2=1,1,1,1 w3=1,1,1,1 w4=1,0,0,0 = 25,4,3,34
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — - `binding_ok`: (critical) "binding" (every party signed the same text version AND every clause reading agreed AND ratified by each party's declared rule) in Ada as Eve + APL as KRON witnessed

**The clause it implements.** - `binding_ok`: (critical) "binding" (every party signed the same text version AND every clause reading agreed AND ratified by each party's declared rule) in Ada as Eve + APL as KRON witnessed

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

### Entry 2 — - `parties`: party hierarchy in MUMPS as MYRA

**The clause it implements.** - `parties`: party hierarchy in MUMPS as MYRA

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

### Entry 3 — never re-derive the tables

**The clause it implements.** never re-derive the tables

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

