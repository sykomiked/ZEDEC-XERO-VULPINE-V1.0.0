# sundial — the complete manifest (the parameter document)

**Purpose.** clock, alarms, timers, world time. Read time from `sys.info`. zxvd has no alarm, timer, time-zone or notice-post method: use only existing methods; list what's missing in your report.

**Matrix layer 110 · icon group L1 · wave spec `wave9/110-sundial.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id sundial
matrix 110
name Sundial
needs sys.read
needs notify

provides flags_none truth bits forbidden
provides limit_excess num value limit
provides count_ok truth count cap
provides composite_key num high low
logic kron flags_none <<
(_bits&_forbidden)=0
.
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
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
check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — local time for a zone (UTC minutes + offset, wrapped to 0..1439) in kron + trebago witnessed

**The clause it implements.** local time for a zone (UTC minutes + offset, wrapped to 0..1439) in kron + trebago witnessed

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

