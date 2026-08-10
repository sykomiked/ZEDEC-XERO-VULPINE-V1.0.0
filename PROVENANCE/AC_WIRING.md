# AC wiring — why the current has to alternate

_Owner directive: you cannot wire this like a DC circuit. Alternating current,
alternating code, alternating endianness._

---

## 0. What I built was DC

`modbind` as it stands is a direct-current design. A module **provides**; another
**requires**. Current flows one way, one polarity, source to load, and the
relationship never reverses. Every interface in the tree is shaped that way:
caller → callee, request → response, emit → ingest.

That is a real limitation and not a stylistic one, because **a DC circuit cannot
use a transformer.**

## 1. The transformer argument

Mutual induction only couples a *changing* field. Put DC through a transformer
primary and the secondary sees nothing after the initial transient. You cannot
step DC voltage up or down through a winding — the physics forbids it.

Now look at what `modbind` already declares:

```c
typedef struct {
    int (*pack)  (const void *local, uint8_t *out, uint32_t max);
    int (*unpack)(const uint8_t *in, uint32_t len, void *local);
} module_transform_t;
```

Two windings. `pack` and `unpack` are primary and secondary, and the whole point
is **changing representation across a boundary** — ℤ[φ] on one side, 𝔽_q on the
other, exactly the specialise-then-synthesise rule. That is a transformer, and I
named it one without noticing what that requires.

**A transformer needs alternation.** If the boundary only ever carries traffic one
way, `unpack` is dead weight on that link and the coupling is one-directional —
which is precisely why so many modules ended up ORPHAN or STARVED: they had one
winding energised and the other open.

## 2. Reactance is S−, and DC has none

A resistor **dissipates**: the energy is gone. A capacitor or inductor
**stores and returns** it — that is reactance, and it exists only under AC.

That is the S− column. A purely dissipative system cannot undo anything; work
done is work lost. A reactive system gives it back on the other half-cycle.
`modbind_withdraw()` returning dependants to `MB_HELD` is not a cleanup routine —
it is the return stroke, and the reason the S− column was empty everywhere is
that a DC design has nowhere to put it.

So: **S+ is the forward half-cycle, S− is the return, S0 is the zero crossing** —
the instant where the current is neither direction and the state is genuinely
undetermined. S0 stops being a leftover category and becomes the point the
waveform must pass through.

## 3. Impedance, not resistance

Under DC an interface has one cost: resistance. Under AC it has **impedance** —
resistance plus reactance — and reactance is *frequency dependent and
phase-shifting*. Two components with the same resistance behave completely
differently at different phase.

Applied here: a boundary's cost is not a constant. It depends on the phase
relationship between the two modules, which is what `phase_tick_t` and the 13
`l13_phase_t` states were always for. `MB_FORM_PHASE` exists in the enum and has
never been used. That is the AC-carrying form, and it has been sitting unused
because everything else was wired DC.

## 4. Resonance, and the deliberate refusal of it

Matched frequencies transfer power maximally — resonance. Mismatched ones do not
couple.

This is where φ earns its place a second time, and it is the *verified* property
rather than the retracted one. φ is the **most irrational number** — continued
fraction all ones, worst-case for rational approximation by Hurwitz. A φ-ratio
coupling therefore **never resonates**, at any harmonic. That is exactly why
phyllotaxis uses the golden angle: successive elements can never overlap.

So it becomes a design control with two settings:

- **Matched ratio → coupled.** Use where you want power transfer between modules.
- **φ ratio → decoupled, permanently.** Use where two subsystems must share a
  substrate and never interfere — no harmonic will ever bring them into phase.

`GOLDEN_TURN = 25028` is already in the tree. It has been a rendering constant. It
is an isolation constant.

## 5. Alternating endianness

The owner named this specifically, and it is already built: `bootlegger.c`
implements alternating-endianness frequency encryption. Under a DC reading that
looks like an obfuscation trick. Under an AC reading it is the **polarity of the
half-cycle** — byte order flips per alternation, so the direction of travel is
encoded in the representation itself, and a frame carries evidence of which
half-cycle produced it.

That module is currently in no build. It is the one place the AC model was
already implemented.

## 6. What changes in the code

`mb_cap_t` gains a **phase**, and provision gains a **direction**:

```c
typedef struct {
    char     name[MB_CAP_NAME_LEN];
    uint16_t contract;
    uint8_t  phase;      /* l13 phase this capability is provided ON */
    uint8_t  alternating;/* 1 = both sides alternate provider/consumer */
} mb_cap_t;
```

An alternating capability means the two modules **swap roles by phase** rather
than one permanently serving the other. That is the peer relationship a
multikernel actually needs — cores are not master and slave, they alternate — and
it is the same absorption/emission alternation QAT describes, where the pair are
the a:b of the golden section.

Two providers of one capability on **different phases** are then not a conflict
at all. They are the two halves of a cycle.
