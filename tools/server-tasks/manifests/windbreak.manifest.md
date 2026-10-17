# windbreak — the complete manifest (the parameter document)

**Purpose.** Fronts the Pit Boss and the Cage (docs/PIT_BOSS.md, docs/CAGE.md; admission in kernel/src/immigration, capabilities in kernel/src/capctl, slot, layer, budget and needs in kernel/src/apps/zxapp.c via `zxapp_slot_layer` and `zxapp_needs`, scheduling in kernel/src/sched). Shows consumers and their energy (`power.top`, `power.status`), the machine (`sys.info`), engines and switches (`features.get`), agents and budgets (`swarm.state`), events and rule actions (`log.tail`, `journal.query`). A running-program table, the Cage's cleared/held/refused queue and the rulebook have no method: use only existing methods; list what's missing in your report.

**Matrix layer 222 · icon group L2 · wave spec `wave18/222-windbreak.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id windbreak
matrix 222
name Windbreak
needs sys.read
needs journal
needs swarm
needs notify

provides count_ok truth count cap
provides limit_excess num value limit
provides composite_key num high low
provides running_state truth state event
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
logic kron composite_key <<
0|_high*1000+_low
.
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — - an item's clearance joins its checks with `lpres_conjoin` in C (kernel order). docs/CAGE.md says "Belnap conjunction"

**The clause it implements.** - an item's clearance joins its checks with `lpres_conjoin` in C (kernel order). docs/CAGE.md says "Belnap conjunction"

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

### Entry 2 — CLAUDE.md says kernel order, not Belnap: use `lpres_conjoin` and report the conflict

**The clause it implements.** CLAUDE.md says kernel order, not Belnap: use `lpres_conjoin` and report the conflict

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

### Entry 3 — Paraconsistent (BOTH test, in test_zxapp.c): check A clears an item (TRUE) and check B refuses it (FALSE), giving BOTH

**The clause it implements.** Paraconsistent (BOTH test, in test_zxapp.c): check A clears an item (TRUE) and check B refuses it (FALSE), giving BOTH

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

