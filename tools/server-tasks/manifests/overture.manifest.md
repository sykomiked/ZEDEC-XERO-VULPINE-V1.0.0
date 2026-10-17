# overture — the complete manifest (the parameter document)

**Purpose.** Fronts first start (kernel/src/seats, kernel/src/ident, kernel/src/terms (the agreement), kernel/src/license, kernel/src/i18n, kernel/src/timesvc for zones). Seats and passkeys (`seats.add`, `seats.list`, `seats.set_lock`, `seats.auto`, `seats.set_auto`, `passkeys.register_begin`, `passkeys.register_finish`), language and simple mode (`xlate.languages`, `ui.get`, `ui.set`), network (`net.command`), moving in from an old machine (`devmesh.join`, `devmesh.confirm_sas`, `files.cross`, `spaces.list`), the clock (`sys.info`). Recording a licence acceptance, setting the time zone and setting the keyboard layout have no method: use only existing methods; list what's missing in your report.

**Matrix layer 252 · icon group L2 · wave spec `wave20/252-overture.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id overture
matrix 252
name Overture
needs sys.read
needs fs.read
needs fs.write
needs peers
needs net
needs backup
needs notify

provides count_ok truth count cap
provides composite_key num high low
provides running_state truth state event
logic eve count_ok <<
eve {app}
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
  cap: counts 100 .. 100
}
.
logic kron composite_key <<
0|((_high*1000)+_low)
.
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check count_ok count=2 cap=100 = true
check count_ok count=99 cap=100 = true
check count_ok count=101 cap=100 = false
check count_ok count=100 cap=100 = true
check count_ok count=0 cap=100 = false
check count_ok count=1 cap=100 = true
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — - (critical) "this seat may be used" = the licence accepted by this user (never inherited from another seat) in Ada as Eve + APL as KRON witnessed

**The clause it implements.** - (critical) "this seat may be used" = the licence accepted by this user (never inherited from another seat) in Ada as Eve + APL as KRON witnessed

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

**Rules that the node enforces for this runtime:** the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

### Entry 2 — - (critical) local time from UTC minutes and the zone's offset and daylight rule in FORTRAN as Trebago + APL as KRON witnessed

**The clause it implements.** - (critical) local time from UTC minutes and the zone's offset and daylight rule in FORTRAN as Trebago + APL as KRON witnessed

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

**A real block from the built apps in this runtime — copy
this spelling exactly (`used.sutra`):**

```
record used-share
  total: 9(18)
  left: 9(18)
  used: 9(5)
procedure
  compute used = (total - left) * 10000 / total
  answer used
```

**The check (one line; the inputs are integers, the answers the
rows):**

```
check <entry_name> <arg>=<int>,... = <answer>,...
```

**Rules that the node enforces for this runtime:** the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.

### Entry 3 — - language and layout tables (language x layout x script) in APL as KRON

**The clause it implements.** - language and layout tables (language x layout x script) in APL as KRON

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

**A real block from the built apps in this runtime — copy
this spelling exactly (`count.tb`):**

```
trebago kind_count
  in  kind(n), want
  out count
  count = sum(i, max(0, min(1, 1 + kind(i) - want)) * max(0, min(1, 1 + want - kind(i))))
end
```

**The check (one line; the inputs are integers, the answers the
rows):**

```
check <entry_name> <arg>=<int>,... = <answer>,...
```

**Rules that the node enforces for this runtime:** the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).

### Entry 4 — Paraconsistent (BOTH test, in test_zxapp.c): the old machine's text size 18 and the new machine's 14 give BOTH

**The clause it implements.** Paraconsistent (BOTH test, in test_zxapp.c): the old machine's text size 18 and the new machine's 14 give BOTH

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

**A real block from the built apps in this runtime — copy
this spelling exactly (`transfer.eve`):**

```
eve coffer
minor_amount = 1 .. 9007199254740991
minor_left = 0 .. 999999999999999999
minor_gap = 0 .. 0

transfer_rule {
  amount: minor_amount
  after: minor_left
  room: minor_left
  gap: minor_gap
}
```

**The check (one line; the inputs are integers, the answers the
rows):**

```
check <entry_name> <arg>=<int>,... = <answer>,...
```

**Rules that the node enforces for this runtime:** the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.

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

**A real block from the built apps in this runtime — copy
this spelling exactly (`use.kron`):**

```
(asked>0)&(granted>0)&revoked<1
```

**The check (one line; the inputs are integers, the answers the
rows):**

```
check <entry_name> <arg>=<int>,... = <answer>,...
```

**Rules that the node enforces for this runtime:** the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.

