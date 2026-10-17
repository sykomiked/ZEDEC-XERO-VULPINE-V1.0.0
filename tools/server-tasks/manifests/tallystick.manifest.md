# tallystick — the complete manifest (the parameter document)

**Purpose.** make invoices and receipts, record payments, render statements (`ledger.post`, `ledger.entries`, `iso20022.render` camt.053), save and print them through the Banker (`files.write`, `print.submit`).

**Matrix layer 78 · icon group L1 · wave spec `wave4/078-tallystick.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id tallystick
matrix 78
name Tallystick
needs ledger.read
needs ledger.post
needs fs.read
needs fs.write
needs print

provides product num a b
provides running_state truth state event
provides composite_key num high low
logic kron product <<
0|_a*_b
.
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
logic kron composite_key <<
0|((_high*1000)+_low)
.
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — "paid in full" truth (payments at or above total) in eve + sutra witnessed

**The clause it implements.** "paid in full" truth (payments at or above total) in eve + sutra witnessed

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

