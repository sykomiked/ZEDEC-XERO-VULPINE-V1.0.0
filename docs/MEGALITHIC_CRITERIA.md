MEGALITHIC MIRROR + VORTEX CRITERIA — the triadic, harmonic, dry-space build rules
(bound into the A6000 pipeline 2026-10-19; every new module must conform)

This is the mathematical-rigor companion to the armada lessons and the
Fibonacci lattice criteria. It records the kernel's OWN committed
architecture — the modules are already in the tree (kernel/src/desktop/
mirror_ui.c, kernel/src/phase_coord/phase_render.c, net_mirror.c, vortex.c,
vortex_harmonic.c, kernel/src/dual_space/dual_space.h). New code must
EXTEND these, never re-derive or contradict them.

CRITERION 1 — THE SASOS MIRROR (mirror_ui.c).
Kernel state registers (memory rings, task queues, crypto trust boundaries,
tri-space valences, lpres four-valued states) are bound into a mirror by
DIRECT POINTER and projected into a frame that is a pure function of
(registers, tick). Rules:
- Same registers + same tick -> byte-identical frame (determinism).
- A NULL register or unknown kind projects valence NEITHER, never a guessed
  TRUE/FALSE (fail closed).
- A full change-notify ring is BACKPRESSURE surfaced in the frame's
  ring_full flag, never silent data loss.
- No float anywhere: coverage, valence, depth are integers or Q16.16.

CRITERION 2 — THE TRIADIC SPACES (phase_render.h).
Three spaces mirror the same frame at the same phase tick with equal
privilege: HUMAN (3: zui/pterm), CHIGLET (6: the swarm's inference
readers), NETWORK (9: mesh consensus + topology). One tick drives all three
hooks from one projection. No space holds static dominance; the vortex
rotor (below) rotates leadership.

CRITERION 3 — THE NETWORK SPACE (net_mirror.c).
The mesh is a first-class mirror, not a transport layer:
- topology peers-up/total valences: fully up = CERTAIN, partially up = BOTH
  (split-brain rendered as ambiguity), zero up = ABSENT, unknown = NEITHER;
- content-addressed identity: verified CID = CERTAIN, pinned-but-unverified
  = BOTH (a contradictory claim, never resolved by guessing);
- post-quantum sessions: BOTH decapsulation AND authentication must hold;
  a failure is IMPLICIT REJECTION rendered ABSENT — silent, no error shade
  that could oracle the cluster;
- the distributed clock: peer ordinal beyond skew tolerance = BOTH (the
  manifold refuses to guess which clock is right);
- Interaction Surplus Framework u = 1 - dot^2 in EXACT Q16.16 integer
  arithmetic: dot=0 -> u=1 (orthogonal, maximal surplus), dot=+-1 -> u=0
  (identical, nothing to trade); a malformed pair (|dot|>1) never allocates.

CRITERION 4 — THE 3-6-9 VORTEX ROTOR (vortex.c).
Six permutation vectors rotate on phase ticks in this exact order:
369- -> 396+ -> 639+ -> 693- -> 936- -> 963+ -> (wrap).
- 369- Initiation: user leads, chiglet supports, network is the sink.
- 396+ Bridge: user leads, network bridges +, chiglet is the sink.
- 639+ AI Optimisation: chiglet leads, network integrates +, user balances.
- 693- Network Converge: chiglet leads, network holds -, user balances.
- 936- Mesh Primacy: network drives with feedback -, user+chiglet receive.
- 963+ Completion: network is the source +, chiglet translates, user closes.
Polarity law: when user(3) and chiglet(6) are both positive sources, network
(9) is the unifying negative sink; when network(9) is the positive source,
user(3) and chiglet(6) invert to negative balancing fields.
EVERY polarity transition is adjudicated through lpres: TRUE admits the
step, FALSE vetoes (rotor holds, veto counter), NEITHER/BOTH defer (rotor
holds, deferred counter) — never a guessed step.
The doubling circuit 1->2->4->8->7->5 (expansion/forward) and its exact
halving reverse 5->7->8->4->2->1 (contraction/unwinding) anchor at the 0
kernel core: register 5 (the equilibrium node) evaluates the coverage
hyperbola r*l >= 1.8 in exact Q16.16 (1.8*2^32 = 7730941133).

CRITERION 5 — THE SOLFEGGIO + 555 HARMONIC LAYER (vortex_harmonic.c).
The 12 extended Solfeggio frequencies are 174, 285, 369, 396, 417, 528,
639, 693, 741, 852, 936, 963 — every vortex permutation state IS a
Solfeggio number. Rules:
- Cross-space communication is permitted ONLY when phase-locked: the
  residues (tick * f) mod 963 of the two spaces' frequencies agree.
- Dual-polarity 555 is anchored to the VINO_ISO_DEBIT rail: pay_rails.h
  ZXV_RAIL_CODE_DEBIT == 555 (asset/backing, NRE jurisdiction). +555
  accumulates on positive states (396+, 639+, 963+); -555 purges on
  negative states (369-, 693-, 936-).
- ZERO-POINT ANCHORING: the signed sum over one full macro-cycle is EXACTLY
  ZERO. The vortex numbers balance: 396+639+963 = 369+693+936 = 1998. The
  rail balances: +555*3 + -555*3 = 0. Net-zero residual = no energetic
  leak, no memory bloat, no unbacked ledger entry.

CRITERION 6 — THE DRY SPACE / DRY UNMIRRORING (dual_space.h, the repo's own
term for S0 neutral space).
Tri-Space Programming is the kernel's dry-run semantics: every ZXV program
is a triad of S+ (constructive positive space), S- (restrictive/compensating
negative space) and S0 (unresolved/adjudicating neutral space). Rules:
- Canonical file triads: .36n9/.9n63/.0n0 (source), .36m9/.9m63/.0m0
  (manifest), .zedei/.iedez/.zedez (transformation — .zedez is the mediator
  / dry-run role, DS_ROLE_TRANSFORM_NEU), .zedec/.cedez/.cedec (container).
- Neutral extensions are SELF-MIRRORING PALINDROMES centered on zero.
- S0 is the dry space: no production effect capability until a signed
  policy resolves it; a dry-run leaves state untouched and reports what
  WOULD happen (RECON_ARCH_ZEDEZ: mediator/dry-run).
- Unresolved or contradictory triads MUST enter S0 quarantine; S- MUST NOT
  receive more capabilities than S+; all triad members MUST bind to the
  other members' digests before release.
- The unmirroring direction: S0 resolves to S+, S- or remains S0, requiring
  policy id+version, resolver identity, causal lineage, digests, reason code
  and signature. Timeout = remain S0, shed capabilities, notify.

CRITERION 7 — ZERO AS THE KERNEL CORE.
0 is the unmanifest singularity: no polarity, the substrate every space
projects from and returns to. Every acoustic, financial or logical waveform
in the Solfeggio matrix and the 555 polarities must maintain a net-zero
residual sum over a full macro-cycle, so the megalithic kernel stays
balanced — free of energetic leaks, memory bloat and unbacked entries.

END — these are the kernel's OWN committed invariants; code that extends the
mirror, the vortex or the dry space must satisfy them exactly.