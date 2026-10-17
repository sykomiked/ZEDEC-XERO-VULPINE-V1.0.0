# wordhoard — the complete manifest (the parameter document)

**Purpose.** word lists kept in the Hoard (`files.read`, `search.query`), the language of a word (`xlate.detect`, `xlate.languages`); a definition from the model (`swarm.ask`) is held until the owner accepts it.

**Matrix layer 46 · icon group L1 · wave spec `wave6/046-wordhoard.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id wordhoard
matrix 46
name Wordhoard
needs fs.read
needs search
needs ai.ask

provides composite_key num high low
provides share_x100 num part whole
logic kron composite_key <<
0|_high*1000+_low
.
logic kron share_x100 <<
0|_part*10000%_whole
.
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — "did you mean" edit distance over code points in C++ wasm

**The clause it implements.** "did you mean" edit distance over code points in C++ wasm

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

