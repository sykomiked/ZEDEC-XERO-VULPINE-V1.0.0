# sieve — the complete manifest (the parameter document)

**Purpose.** every held suggestion (M3) in one place: tagged answers (`swarm.ask`), untrusted drafts (`play.draft`), held envelopes (`web4.feed`, `web4.consent`), and what was accepted (`journal.query`).

**Matrix layer 63 · icon group L1 · wave spec `wave3/063-sieve.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id sieve
matrix 63
name Sieve
needs ai.ask
needs journal
needs notify

provides count_ok truth count cap
provides limit_excess num value limit
provides composite_key num high low
logic eve count_ok <<
eve {app}
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
  cap: counts 100 .. 100
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
0|((_high*1000)+_low)
.
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check count_ok count=2 cap=100 = true
check count_ok count=99 cap=100 = true
check count_ok count=101 cap=100 = false
check count_ok count=100 cap=100 = true
check count_ok count=0 cap=100 = false
check count_ok count=1 cap=100 = true
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check limit_excess value=1000,999 limit=1000,1000 = 0,0
check limit_excess value=1,1000000 limit=0,999999 = 1,1
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — "may this be applied" (accepted, not expired, target unchanged since) in eve + kron + C++ wasm (three witnesses)

**The clause it implements.** "may this be applied" (accepted, not expired, target unchanged since) in eve + kron + C++ wasm (three witnesses)

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

### Entry 2 — Nothing is applied without the owner's accept

**The clause it implements.** Nothing is applied without the owner's accept

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
this spelling exactly (`name_ok.kron`):**

```
(label>0)&(label<64)&(total>0)&(total<254)&(bad=0)&(total-label)>-1
```

**The check (one line; the inputs are integers, the answers the
rows):**

```
check <entry_name> <arg>=<int>,... = <answer>,...
```

**Rules that the node enforces for this runtime:** the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.

### Entry 3 — an accept is logged

**The clause it implements.** an accept is logged

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
this spelling exactly (`limit.eve`):**

```
eve shepherd
minute_of_day = 0 .. 1439
daily_cap = -1 .. 1440

limit_rule {
  minutes: daily_cap
  from: minute_of_day
  to: minute_of_day
}
```

**The check (one line; the inputs are integers, the answers the
rows):**

```
check <entry_name> <arg>=<int>,... = <answer>,...
```

**Rules that the node enforces for this runtime:** the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.

