# assay — the complete manifest (the parameter document)

**Purpose.** run test prompts against the swarm's agents and score them (`swarm.state` agents and audit, `swarm.ask` under the budget gate, `play.run` for scored templates on scratch state).

**Matrix layer 72 · icon group L1 · wave spec `wave2/072-assay.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id assay
matrix 72
name Assay
needs swarm
needs ai.ask
needs sys.read

provides share_x100 num part whole
provides count_ok truth count cap
provides clamp num value lo hi
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
logic kron clamp <<
0|max(_lo, min(_hi, _value))
.
check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check clamp value=-5,0,50,150 lo=0,0,0,0 hi=100,100,100,100 = 0,0,50,100
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — "meets the bar" truth (passes, refusals, minimum runs) in eve + kron witnessed

**The clause it implements.** "meets the bar" truth (passes, refusals, minimum runs) in eve + kron witnessed

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

