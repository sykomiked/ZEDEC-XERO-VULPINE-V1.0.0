# aureole — the complete manifest (the parameter document)

**Purpose.** runs a town or co-op council: agendas, motions, minutes, budgets and signed decisions in one public record (`spaces.propose`, `spaces.vote`, `spaces.get`, `journal.query`, `web4.verify`, `files.write`).

**Matrix layer 277 · icon group L3 · wave spec `wave23/277-aureole.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id aureole
matrix 277
name Aureole
needs peers
needs net
needs journal
needs fs.write
needs notify

provides share_x100 num part whole
provides count_ok truth count cap
provides limit_excess num value limit
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
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — `revenue` (next year's revenue from the trend) in FORTRAN as Trebago

**The clause it implements.** `revenue` (next year's revenue from the trend) in FORTRAN as Trebago

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

### Entry 2 — `bylaws` hierarchy in MUMPS as MYRA

**The clause it implements.** `bylaws` hierarchy in MUMPS as MYRA

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

