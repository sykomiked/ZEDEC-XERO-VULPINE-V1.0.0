# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The paradox engine: four kinds, fixed logged operators, complex-paradox graphs
(ABI 1.5, kernel ``paradox``).

A PARADOX here is work whose four-valued state is Both (the evidence points
both ways) or Neither (no evidence). This group does not decide which side is
"really" true. It reads the work's :class:`~zxv.Hexagram` tag (there is no
second tag type), gives the paradox one of four :class:`ParadoxKind` values,
and runs a fixed, logged operator for that kind:

============  ==============================  ===================================
Kind          When                            Operator (on :class:`ParadoxBook`)
============  ==============================  ===================================
CONSTRUCTIVE  Both, margin growing            :meth:`~ParadoxBook.constructive`:
                                              a bounded :class:`ParadoxPolicy` update
DESTRUCTIVE   Both, margin shrinking / stale  :meth:`~ParadoxBook.destructive`:
                                              quarantine, roll back, RepairProof
NEUTRAL       Both and steady, or Neither     :meth:`~ParadoxBook.neutral`: keep
                                              every branch of a :class:`ParadoxHold`
SPECIAL       Both, steady, potential phase,  :meth:`~ParadoxBook.special`: a
              no registry signature           permanent frontier record + review
============  ==============================  ===================================

:meth:`ParadoxBook.classify` returns a :class:`Classification` (the kind and
the tag with the kind in its polarity); every operator takes one. A
FORBIDDEN tag means a real check found a broken invariant: every operator
refuses it and rolls back. A registry signature (:func:`signature`) overrides
the trend.

An operator that ran returns its :class:`ParadoxOutcome` (REFUSED and
PROPOSAL_FOR_OWNER are decisions, not errors) and has appended one record to
the operator log. An exception means it did not run and nothing changed, with
two documented exceptions (see :meth:`ParadoxBook.destructive` and
:meth:`ParadoxBook.graph_pass`). A full log is ``ZXV_E_CAPACITY``: halt the
context, because work that cannot be recorded must not continue.

Capabilities: writing a log or the registry, applying a policy update, and
creating any handle need ``Cap.PARADOX`` from a cap of the book's context.
Reading, classifying, adding branches, nodes and updates to a handle you
hold, and the pure functions need none. Nothing here signs: sign proofs,
drafts and proposals yourself with :class:`zxv.Ed25519` or :class:`zxv.MlDsa87`.

Honest limits, in plain words:

* Automatic changes happen only inside published bounds; an update with any
  value outside its ``[lo, hi]`` changes nothing and becomes a proposal for
  the owner. The operator has no balance input: it cannot move money.
* Policies that touch money or devices need the owner. ``owner_ok`` is your
  assertion: inside one process this library cannot check who the owner is.
* Detection is by these checks and not instant: what they do not look at
  goes undetected.
* Destructive work is contained, never answered: there is no way to reply
  to, or act against, a sender.
* This is contradiction-tolerant bookkeeping; it never makes both sides true.
* Peers agree because they apply the same fixed order to the same signed
  inputs, not through a shared clock. Integers only.

Threading: a :class:`ParadoxBook` is internally serialized. Policy, hold and
graph handles are not thread-safe in C; the SDK serializes calls on each.
"""

from __future__ import annotations

import enum
import struct
from dataclasses import dataclass
from typing import Any, Callable, Iterator, List, Optional, Sequence, Tuple

from . import _native
from ._handle import NativeHandle, SerializedHandle
from ._native import ffi
from .context import Capability
from .hexagram import HexAction, Hexagram

__all__ = [
    "PDX_HASH_BYTES", "PDX_REC_BYTES", "PDX_QREC_BYTES", "PDX_REPAIR_BYTES", "PDX_DRAFT_BYTES",
    "PDX_LOG_MAX", "PDX_REGISTRY_MAX", "PDX_POLICY_MAX", "PDX_HOLD_MAX", "PDX_GRAPH_MAX", "PDX_MAX_DEPS",
    "PDX_UPDATES_MAX", "PDX_TRACE_MAX", "PDX_SRC_SEED", "ParadoxKind", "ParadoxDo", "ParadoxOutcome",
    "NodeState", "ParadoxFlag", "Trip", "ParadoxLog", "Classification", "RepairProof", "signature", "classify",
    "kind_of_tag", "action", "outcome_action", "proposal_bytes", "ParadoxBook", "RegistryEntry",
    "DestructiveResult", "NeutralResult", "SpecialResult", "ParadoxPolicy", "PolicyState",
    "PolicyBounds", "ParadoxHold", "HoldInfo", "HoldBranch", "ParadoxGraph", "GraphNode", "GraphInfo",
]

PDX_HASH_BYTES = 32
PDX_REC_BYTES = 144
"""An operator or frontier log record."""
PDX_QREC_BYTES = 110
"""A quarantine log record."""
PDX_REPAIR_BYTES = 160
"""A RepairProof."""
PDX_DRAFT_BYTES = 71
"""A draft classification."""
PDX_LOG_MAX = 65536
PDX_REGISTRY_MAX = 65536
PDX_POLICY_MAX = 64
PDX_HOLD_MAX = 64
PDX_GRAPH_MAX = 64
PDX_MAX_DEPS = 4
PDX_UPDATES_MAX = 256
PDX_TRACE_MAX = 4096
PDX_SRC_SEED = 2**64 - 1
"""``src`` of a registry entry not learned from the frontier."""


def _name(fn: Callable[[int], Any], v: int) -> str:
    return str(ffi.string(fn(v)).decode("ascii"))


class ParadoxKind(enum.IntEnum):
    """The four kinds, and NONE for True/False (``ZXV_PDX_KIND_*``)."""

    CONSTRUCTIVE = 0
    """Catalyst: a bounded parameter update."""
    DESTRUCTIVE = 1
    """Immune: contain, quarantine, roll back."""
    NEUTRAL = 2
    """Hold every candidate branch."""
    SPECIAL = 3
    """Unclassified: the frontier."""
    NONE = 4
    """True or False: not a paradox."""

    @property
    def label(self) -> str:
        """The library's name for it (``"constructive"``)."""
        return _name(_native.lib().zxv_paradox_kind_name, self.value)


class ParadoxDo(enum.IntEnum):
    """What to do with a classified paradox (``ZXV_PDX_DO_*``)."""

    NONE = 0
    """Not a paradox: use :attr:`Hexagram.action`."""
    SYNTHESIZE = 1
    """Constructive: :meth:`ParadoxBook.constructive`."""
    CONTAIN = 2
    """Destructive: :meth:`ParadoxBook.destructive`."""
    HOLD = 3
    """Neutral: :meth:`ParadoxBook.neutral`."""
    RECORD = 4
    """Special: :meth:`ParadoxBook.special` (sandbox)."""
    ROLLBACK = 5
    """Forbidden: refuse, roll back, quarantine."""


class ParadoxOutcome(enum.IntEnum):
    """Operator outcomes (``ZXV_PDX_OUT_*``)."""

    APPLIED = 0
    KEPT = 1
    PROPOSAL_FOR_OWNER = 2
    ROLLED_BACK = 3
    CONTAINED = 4
    DEFERRED = 5
    SETTLED = 6
    RECORDED = 7
    ACCEPTED = 8
    PENDING = 9
    RESOLVED = 10
    ESCALATE_TO_OWNER = 11
    REFUSED = 12
    LOG_FULL = 13
    """Never reported: a full log raises ``ZXV_E_CAPACITY`` instead."""

    @property
    def label(self) -> str:
        """The library's name for it (``"proposal-for-owner"``)."""
        return _name(_native.lib().zxv_paradox_outcome_name, self.value)

    @property
    def hex_action(self) -> HexAction:
        """The :class:`HexAction` this outcome amounts to."""
        return outcome_action(self)


class NodeState(enum.IntEnum):
    """Graph node states (``ZXV_PDX_NS_*``). Final: CLEAR, CONTAINED, ROLLED_BACK, TAINTED, APPLIED."""

    OPEN = 0
    CLEAR = 1
    CONTAINED = 2
    ROLLED_BACK = 3
    TAINTED = 4
    SANDBOXED = 5
    WAITING = 6
    HELD = 7
    BLOCKED = 8
    APPLIED = 9

    @property
    def label(self) -> str:
        return _name(_native.lib().zxv_paradox_node_state_name, self.value)


class ParadoxFlag(enum.IntFlag):
    """Flags of a policy, registry entry or draft (``ZXV_PDX_FLAG_*``)."""

    NONE = 0
    MONEY = 1
    """Touches money: the owner decides."""
    DEVICE = 2
    """Touches devices: the owner decides."""


class Trip(enum.IntFlag):
    """Which checks fired (a RepairProof's trips; a quarantine reason)."""

    NONE = 0
    STEPS = 1
    BUDGET = 2
    CYCLE = 4
    INVARIANT = 8
    FORBIDDEN = 16


class ParadoxLog(enum.IntEnum):
    """The book's three logs (``ZXV_PDX_LOG_*``)."""

    OPS = 0
    """The operator log."""
    FRONTIER = 1
    """Special records, never edited."""
    QUARANTINE = 2
    """Contained work (the lpres_hex X6 log)."""


def _u32(v: Any, what: str) -> int:
    return _native.exact_int(v, what, *_native.U32)


def _h32(v: Any, what: str) -> bytes:
    return _native.fixed_bytes(v, 32, what)


def _b(data: bytes) -> Any:
    return _native.cbuf(data)


def _tag(t: Hexagram) -> int:
    if not isinstance(t, Hexagram):
        raise TypeError("tag must be a Hexagram")
    return t.tag


@dataclass(frozen=True)
class Classification:
    """A classified pair (kind, tag), what every operator takes.

    Get one from :meth:`ParadoxBook.classify`; you may also build one by hand
    (a peer's input, say). The library checks the pair on every use and
    refuses an invalid one with ``ZXV_E_ARG``.
    """

    kind: ParadoxKind
    tag: Hexagram
    """The tag with the kind in its polarity (a forbidden tag stays forbidden)."""
    registry_index: Optional[int] = None
    """The registry entry that decided it, if any."""

    @property
    def action(self) -> ParadoxDo:
        """What to do with it (``ROLLBACK`` for a forbidden tag, whatever the kind)."""
        return action(self.kind, self.tag)

    def _k(self) -> Tuple[int, int]:
        return _native.exact_int(int(self.kind), "kind", *_native.I32), _tag(self.tag)


def signature(data: bytes) -> bytes:
    """The signature of an anomaly: SHA-256(``"zxv/pdx-sig/v1"`` || ``data``), from a
    canonical description you build."""
    d = _native.to_bytes(data, "data")
    out = ffi.new("uint8_t[32]")
    _native.check(_native.lib().zxv_paradox_signature(_b(d), len(d), out))
    return bytes(ffi.buffer(out, 32))


def classify(tag: Hexagram, *, evidence: Optional[Tuple[int, int, int, int]] = None,
             stale: bool = False) -> Classification:
    """Classify a tag with no registry lookup (no book, no capability); see
    :meth:`ParadoxBook.classify` for the arguments."""
    ev: Any = ffi.NULL
    if evidence is not None:
        if len(evidence) != 4:
            raise _native.make_error(-1, "evidence must be (pos0, neg0, pos1, neg1)")
        ev = ffi.new("uint32_t[4]", [_u32(x, "evidence") for x in evidence])
    k, t, r = ffi.new("int32_t *"), ffi.new("uint8_t *"), ffi.new("int32_t *")
    _native.check(_native.lib().zxv_paradox_classify(ffi.NULL, _tag(tag), ev, 1 if stale else 0, ffi.NULL,
                                                     k, t, r))
    return Classification(ParadoxKind(k[0]), Hexagram.from_tag(int(t[0])), None)


def kind_of_tag(tag: Hexagram) -> ParadoxKind:
    """The kind a tag reads as on its own (a forbidden tag reads as NONE)."""
    out = ffi.new("int32_t *")
    _native.check(_native.lib().zxv_paradox_kind_of_tag(_tag(tag), out))
    return ParadoxKind(out[0])


def action(kind: int, tag: Hexagram) -> ParadoxDo:
    """What to do with a classified pair.

    Raises:
        ZxvUsageError: ``ZXV_E_ARG`` (an invalid pair).
    """
    out = ffi.new("int32_t *")
    _native.check(_native.lib().zxv_paradox_action(_native.exact_int(int(kind), "kind", *_native.I32),
                                                   _tag(tag), out))
    return ParadoxDo(out[0])


def outcome_action(outcome: int) -> HexAction:
    """The :class:`HexAction` an outcome amounts to (APPLIED -> PROCEED, DEFERRED -> WAIT, ...)."""
    out = ffi.new("int32_t *")
    _native.check(_native.lib().zxv_paradox_outcome_action(_native.exact_int(int(outcome), "outcome",
                                                                             *_native.I32), out))
    return HexAction(out[0])


@dataclass(frozen=True)
class RepairProof:
    """A RepairProof (:data:`PDX_REPAIR_BYTES` bytes, little-endian) from a containment.

    Share :attr:`signature` with peers so they can register the trap. Sign
    :attr:`raw` yourself; :meth:`verify` checks integrity only.
    """

    raw: bytes
    trips: Trip
    steps: int
    spent: int
    trace_n: int
    submitter: int
    length: int
    """Length of the contained state."""
    state_found: bytes
    """SHA-256 of the state as found."""
    state_restored: bytes
    """SHA-256 of the state after rollback."""
    signature: bytes
    """The trap's signature (:func:`signature` of the pattern) to share with peers."""
    proof_hash: bytes
    quarantine_index: int
    """Index of the quarantine record it wrote."""

    @classmethod
    def parse(cls, raw: bytes) -> "RepairProof":
        """Split proof bytes into fields (no check; call :meth:`verify`)."""
        b = _native.fixed_bytes(raw, PDX_REPAIR_BYTES, "proof")
        trips, steps, spent, tn, sub, ln = struct.unpack_from("<6I", b, 0)
        (qi,) = struct.unpack_from("<Q", b, 152)
        return cls(b, Trip(trips), steps, spent, tn, sub, ln, b[24:56], b[56:88], b[88:120], b[120:152], qi)

    @staticmethod
    def verify_bytes(raw: bytes) -> None:
        """Recompute the binding hash of proof bytes.

        Raises:
            VerificationFailedError: the bytes were changed.
        """
        b = _native.fixed_bytes(raw, PDX_REPAIR_BYTES, "proof")
        _native.check(_native.lib().zxv_paradox_repair_verify(_b(b)))

    def verify(self) -> None:
        """Recompute this proof's binding hash (integrity only; a signature over it is yours to check)."""
        self.verify_bytes(self.raw)


def proposal_bytes(n: int) -> int:
    """Size of a policy proposal over ``n`` parameters: 12 + 32 * n."""
    return 12 + 32 * n


@dataclass(frozen=True)
class RegistryEntry:
    """One entry of the signature registry."""

    signature: bytes
    kind: ParadoxKind
    reason: int
    flags: ParadoxFlag
    source: Optional[int]
    """The frontier record it was learned from, or ``None`` for a seed entry."""


@dataclass(frozen=True)
class DestructiveResult:
    """What :meth:`ParadoxBook.destructive` decided."""

    outcome: ParadoxOutcome
    """CONTAINED or REFUSED."""
    proof: Optional[RepairProof]
    """The RepairProof when contained."""


@dataclass(frozen=True)
class NeutralResult:
    outcome: ParadoxOutcome
    settled: Optional[int]
    """The settled branch index, if one settled."""


@dataclass(frozen=True)
class SpecialResult:
    outcome: ParadoxOutcome
    record: Optional[int]
    """The frontier record index when RECORDED."""


def _i64s(vals: Sequence[int], what: str) -> Any:
    return ffi.new("int64_t[]", [_native.exact_int(v, what, *_native.I64) for v in vals])


# ---------------------------------------------------------------- handles


@dataclass(frozen=True)
class PolicyState:
    """A policy's live values, the values before the last applied update,
    whether that update awaits evaluation, and the tension it was applied at."""

    values: Tuple[int, ...]
    previous: Tuple[int, ...]
    armed: bool
    tension_at: int


@dataclass(frozen=True)
class PolicyBounds:
    lo: Tuple[int, ...]
    hi: Tuple[int, ...]
    flags: ParadoxFlag


class ParadoxPolicy(SerializedHandle):
    """Constructive parameters with published bounds (``zxv_paradox_policy``).

    The values are swarm parameters (budget shares, routing weights, escrow
    timing), never balances. The bounds never change. A policy flagged
    MONEY or DEVICE is never changed automatically.

    Args:
        cap: a capability holding ``Cap.PARADOX``.
        lo, hi, initial: 1..64 values each, ``lo[i] <= initial[i] <= hi[i]``.
        flags: :class:`ParadoxFlag`.

    Raises:
        ZxvUsageError: ``ZXV_E_RANGE`` (``lo > hi`` or an initial value outside its bounds).
    """

    def __init__(self, cap: Capability, lo: Sequence[int], hi: Sequence[int], initial: Sequence[int], *,
                 flags: ParadoxFlag = ParadoxFlag.NONE) -> None:
        n = len(initial)
        if len(lo) != n or len(hi) != n:
            raise _native.make_error(-1, "lo, hi and initial must have the same length")
        ctx = cap.context
        out = ffi.new("zxv_paradox_policy **")
        _native.check(_native.lib().zxv_paradox_policy_create(cap._handle(), n, _i64s(lo, "lo"), _i64s(hi, "hi"),
                                                              _i64s(initial, "initial"), _u32(int(flags), "flags"),
                                                              out), ctx)
        self._n = n
        super().__init__(out[0], cap, ctx)

    def _destroy(self, handle: Any) -> None:
        _native.lib().zxv_paradox_policy_destroy(handle)

    def __len__(self) -> int:
        return self._n

    @property
    def state(self) -> PolicyState:
        v, p = ffi.new("int64_t[]", self._n), ffi.new("int64_t[]", self._n)
        armed, ta = ffi.new("int32_t *"), ffi.new("uint32_t *")
        with self._lock:
            self._check(_native.lib().zxv_paradox_policy_values(self._handle(), v, p, self._n, armed, ta))
        return PolicyState(tuple(int(v[i]) for i in range(self._n)), tuple(int(p[i]) for i in range(self._n)),
                           bool(armed[0]), int(ta[0]))

    @property
    def values(self) -> Tuple[int, ...]:
        """The live values."""
        return self.state.values

    @property
    def bounds(self) -> PolicyBounds:
        lo, hi, f = ffi.new("int64_t[]", self._n), ffi.new("int64_t[]", self._n), ffi.new("uint32_t *")
        with self._lock:
            self._check(_native.lib().zxv_paradox_policy_bounds(self._handle(), lo, hi, self._n, f))
        return PolicyBounds(tuple(int(lo[i]) for i in range(self._n)), tuple(int(hi[i]) for i in range(self._n)),
                            ParadoxFlag(f[0]))

    def proposal(self, synth: Sequence[int]) -> bytes:
        """A proposed update as bytes for the owner or for signing
        (:func:`proposal_bytes` long): n, how many values are out of bounds,
        flags, then n entries of lo, hi, current, proposed (i64 LE). Reads only."""
        if len(synth) != self._n:
            raise _native.make_error(-1, f"synth must have {self._n} values")
        s = _i64s(synth, "synth")
        with self._lock:
            h = self._handle()
            return _native.read_bytes(
                lambda b, c, k: _native.lib().zxv_paradox_policy_proposal(h, s, self._n, b, c, k),
                self._context, proposal_bytes(self._n))


@dataclass(frozen=True)
class HoldInfo:
    count: int
    settled: Optional[int]
    """The settled branch, or ``None`` while deferred."""
    decisive: Optional[int]
    """The branch with decisive evidence now, if any."""


@dataclass(frozen=True)
class HoldBranch:
    id: bytes
    positive: int
    negative: int


class ParadoxHold(SerializedHandle):
    """Candidate branches of a neutral paradox (``zxv_paradox_hold``).

    A branch settles when its net evidence (for minus against) leads every
    other by at least ``decisive`` (0 means 1), or alone reaches it, or the
    owner picks it (:meth:`ParadoxBook.neutral`).

    Args:
        cap: a capability holding ``Cap.PARADOX``.
        capacity: 1..:data:`PDX_HOLD_MAX` branches.
    """

    def __init__(self, cap: Capability, capacity: int, decisive: int = 0) -> None:
        ctx = cap.context
        out = ffi.new("zxv_paradox_hold **")
        _native.check(_native.lib().zxv_paradox_hold_create(cap._handle(), _u32(capacity, "capacity"),
                                                            _u32(decisive, "decisive"), out), ctx)
        super().__init__(out[0], cap, ctx)

    def _destroy(self, handle: Any) -> None:
        _native.lib().zxv_paradox_hold_destroy(handle)

    def add(self, branch_id: bytes) -> None:
        """Add a candidate (a 32-byte digest); the same id again is the same candidate.

        Raises:
            ZxvRefusedError: ``ZXV_E_STATE`` (settled), ``ZXV_E_CAPACITY`` (full).
        """
        i = _h32(branch_id, "branch_id")
        with self._lock:
            self._check(_native.lib().zxv_paradox_hold_add(self._handle(), _b(i)))

    def evidence(self, branch_id: bytes, positive: bool) -> None:
        """One piece of evidence for (``positive``) or against a candidate.

        Raises:
            ZxvUsageError: ``ZXV_E_NOT_FOUND`` (unknown id).
            ZxvRefusedError: ``ZXV_E_STATE`` (settled).
        """
        if not isinstance(positive, bool):
            raise TypeError("positive must be a bool")
        i = _h32(branch_id, "branch_id")
        with self._lock:
            self._check(_native.lib().zxv_paradox_hold_evidence(self._handle(), _b(i), 1 if positive else 0))

    @property
    def info(self) -> HoldInfo:
        c, s, d = ffi.new("uint32_t *"), ffi.new("int32_t *"), ffi.new("int32_t *")
        with self._lock:
            self._check(_native.lib().zxv_paradox_hold_info(self._handle(), c, s, d))
        return HoldInfo(int(c[0]), None if s[0] < 0 else int(s[0]), None if d[0] < 0 else int(d[0]))

    def branch(self, index: int) -> HoldBranch:
        i, p, n = ffi.new("uint8_t[32]"), ffi.new("uint32_t *"), ffi.new("uint32_t *")
        with self._lock:
            self._check(_native.lib().zxv_paradox_hold_branch(self._handle(), _u32(index, "index"), i, p, n))
        return HoldBranch(bytes(ffi.buffer(i, 32)), int(p[0]), int(n[0]))

    def __len__(self) -> int:
        return self.info.count

    def __iter__(self) -> Iterator[HoldBranch]:
        for i in range(len(self)):
            yield self.branch(i)


@dataclass(frozen=True)
class GraphNode:
    id: bytes
    kind: ParadoxKind
    state: NodeState


@dataclass(frozen=True)
class GraphInfo:
    nodes: int
    passes: int
    total_mag: int
    """Net tension magnitude of the open nodes after the last pass."""
    total_dir: int
    """Its direction: -1, 0 or +1."""
    escalated: bool
    chain: bytes
    """The SHA-256 chain over every pass (zeros before the first)."""


class ParadoxGraph(SerializedHandle):
    """A complex paradox: a dependency graph of up to :data:`PDX_GRAPH_MAX` nodes
    (``zxv_paradox_graph``) resolved pass by pass with :meth:`ParadoxBook.graph_pass`.
    It escalates to the owner after ``step_limit`` passes without resolving.

    Nodes are taken in id order, so every peer with the same signed inputs
    reaches the same states and chain.
    """

    def __init__(self, cap: Capability, step_limit: int) -> None:
        ctx = cap.context
        out = ffi.new("zxv_paradox_graph **")
        _native.check(_native.lib().zxv_paradox_graph_create(cap._handle(), _u32(step_limit, "step_limit"), out),
                      ctx)
        super().__init__(out[0], cap, ctx)

    def _destroy(self, handle: Any) -> None:
        _native.lib().zxv_paradox_graph_destroy(handle)

    def add_node(self, node_id: bytes, pair: Classification, *, mag: int = 0, direction: int = 0) -> None:
        """Add a node before the first pass: ``node_id`` is the hash of its signed
        input; ``mag`` and ``direction`` (-1, 0, +1) its tension.

        Raises:
            ZxvUsageError: ``ZXV_E_DUPLICATE``, ``ZXV_E_ARG``.
            ZxvRefusedError: ``ZXV_E_CAPACITY``, ``ZXV_E_STATE`` (after the first pass).
        """
        k, t = pair._k()
        i = _h32(node_id, "node_id")
        with self._lock:
            self._check(_native.lib().zxv_paradox_graph_add_node(self._handle(), _b(i), k, t, _u32(mag, "mag"),
                                                                 _native.exact_int(direction, "direction", -1, 1)))

    def add_dep(self, node_id: bytes, dep: bytes) -> None:
        """Node ``node_id`` depends on node ``dep`` (which must exist by the first pass)."""
        a, d = _h32(node_id, "node_id"), _h32(dep, "dep")
        with self._lock:
            self._check(_native.lib().zxv_paradox_graph_add_dep(self._handle(), _b(a), _b(d)))

    def update(self, node_id: bytes, pair: Classification, *, mag: int = 0, direction: int = 0) -> None:
        """Queue new evidence for a node, applied at the start of the next pass. A batch
        merges "worst wins", so it gives the same result in any order."""
        k, t = pair._k()
        i = _h32(node_id, "node_id")
        with self._lock:
            self._check(_native.lib().zxv_paradox_graph_update(self._handle(), _b(i), k, t, _u32(mag, "mag"),
                                                               _native.exact_int(direction, "direction", -1, 1)))

    def state(self, node_id: bytes) -> NodeState:
        """A node's state (OPEN before the first pass)."""
        out = ffi.new("int32_t *")
        i = _h32(node_id, "node_id")
        with self._lock:
            self._check(_native.lib().zxv_paradox_graph_state(self._handle(), _b(i), out))
        return NodeState(out[0])

    def node(self, index: int) -> GraphNode:
        """Node ``index`` in id order (after the first pass)."""
        i, k, s = ffi.new("uint8_t[32]"), ffi.new("int32_t *"), ffi.new("int32_t *")
        with self._lock:
            self._check(_native.lib().zxv_paradox_graph_node_at(self._handle(), _u32(index, "index"), i, k, s))
        return GraphNode(bytes(ffi.buffer(i, 32)), ParadoxKind(k[0]), NodeState(s[0]))

    @property
    def info(self) -> GraphInfo:
        n, p, m = ffi.new("uint32_t *"), ffi.new("uint32_t *"), ffi.new("uint32_t *")
        d, e = ffi.new("int32_t *"), ffi.new("int32_t *")
        ch = ffi.new("uint8_t[32]")
        with self._lock:
            self._check(_native.lib().zxv_paradox_graph_info(self._handle(), n, p, m, d, e, ch))
        return GraphInfo(int(n[0]), int(p[0]), int(m[0]), int(d[0]), bool(e[0]), bytes(ffi.buffer(ch, 32)))

    def __len__(self) -> int:
        return self.info.nodes


class ParadoxBook(NativeHandle):
    """The book (``zxv_paradox``): the operator log, the frontier log, the
    quarantine log and the signature registry. All logs are append-only and
    SHA-256 hash-chained.

    Every writing call uses the book's capability unless you pass another one
    (``cap=``) from the same context. Handles used with it (policies, holds,
    graphs) must belong to the same context.

    Args:
        cap: a capability holding ``Cap.PARADOX``.
        log_capacity: records per log, 1..:data:`PDX_LOG_MAX`.
        registry_capacity: registry entries, 1..:data:`PDX_REGISTRY_MAX`.

    Raises:
        CapabilityDeniedError: no ``Cap.PARADOX``.
    """

    def __init__(self, cap: Capability, log_capacity: int = 1024, registry_capacity: int = 256) -> None:
        ctx = cap.context
        out = ffi.new("zxv_paradox **")
        _native.check(_native.lib().zxv_paradox_create(cap._handle(), _u32(log_capacity, "log_capacity"),
                                                       _u32(registry_capacity, "registry_capacity"), out), ctx)
        self._cap = cap
        super().__init__(out[0], cap, ctx)

    def _destroy(self, handle: Any) -> None:
        _native.lib().zxv_paradox_destroy(handle)

    def _c(self, cap: Optional[Capability]) -> Any:
        return (cap or self._cap)._handle()

    # ------------------------------------------------------------ classify

    def classify(self, tag: Hexagram, *, evidence: Optional[Tuple[int, int, int, int]] = None,
                 stale: bool = False, signature: Optional[bytes] = None) -> Classification:
        """Classify a tag (reads only; no capability).

        Args:
            evidence: ``(pos0, neg0, pos1, neg1)``, the evidence counts before
                and after; ``None`` for no trend (polarity NEUTRAL).
            stale: the newest evidence is past your freshness limit (ignored
                without evidence).
            signature: the anomaly's :func:`signature`; a registered one
                overrides the trend.
        """
        ev: Any = ffi.NULL
        if evidence is not None:
            if len(evidence) != 4:
                raise _native.make_error(-1, "evidence must be (pos0, neg0, pos1, neg1)")
            ev = ffi.new("uint32_t[4]", [_u32(x, "evidence") for x in evidence])
        sg = ffi.NULL if signature is None else _b(_h32(signature, "signature"))
        k, t, r = ffi.new("int32_t *"), ffi.new("uint8_t *"), ffi.new("int32_t *")
        self._check(_native.lib().zxv_paradox_classify(self._handle(), _tag(tag), ev, 1 if stale else 0, sg, k, t, r))
        return Classification(ParadoxKind(k[0]), Hexagram.from_tag(int(t[0])), None if r[0] < 0 else int(r[0]))

    # ------------------------------------------------------------ registry

    def registry_add(self, signature: bytes, kind: ParadoxKind, *, reason: int = 0,
                     flags: ParadoxFlag = ParadoxFlag.NONE, owner_ok: bool = False,
                     cap: Optional[Capability] = None) -> int:
        """Add a reviewed signature as CONSTRUCTIVE, DESTRUCTIVE or NEUTRAL; returns
        its index. Logged (one operator record).

        Raises:
            OwnerApprovalRequiredError: flagged MONEY or DEVICE without ``owner_ok``.
            ZxvUsageError: ``ZXV_E_DUPLICATE`` (registered), ``ZXV_E_ARG``.
            ZxvRefusedError: ``ZXV_E_CAPACITY`` (registry or log full).
        """
        s = _h32(signature, "signature")
        out = ffi.new("uint32_t *")
        self._check(_native.lib().zxv_paradox_registry_add(self._c(cap), self._handle(), _b(s),
                                                           _native.exact_int(int(kind), "kind", *_native.I32),
                                                           _native.exact_int(reason, "reason", 0, 255),
                                                           _native.exact_int(int(flags), "flags", 0, 255),
                                                           1 if owner_ok else 0, out))
        return int(out[0])

    def registry_find(self, signature: bytes) -> Optional[int]:
        """The index of a signature, or ``None``."""
        out = ffi.new("uint32_t *")
        rc = _native.lib().zxv_paradox_registry_find(self._handle(), _b(_h32(signature, "signature")), out)
        if rc == -4:
            return None
        self._check(rc)
        return int(out[0])

    @property
    def registry_count(self) -> int:
        out = ffi.new("uint32_t *")
        self._check(_native.lib().zxv_paradox_registry_count(self._handle(), out))
        return int(out[0])

    def registry_entry(self, index: int) -> RegistryEntry:
        s = ffi.new("uint8_t[32]")
        k, r, f, src = ffi.new("int32_t *"), ffi.new("uint8_t *"), ffi.new("uint8_t *"), ffi.new("uint64_t *")
        self._check(_native.lib().zxv_paradox_registry_at(self._handle(), _u32(index, "index"), s, k, r, f, src))
        return RegistryEntry(bytes(ffi.buffer(s, 32)), ParadoxKind(k[0]), int(r[0]), ParadoxFlag(f[0]),
                             None if src[0] == PDX_SRC_SEED else int(src[0]))

    # ------------------------------------------------------------ logs

    def log_count(self, which: ParadoxLog) -> int:
        out = ffi.new("uint32_t *")
        self._check(_native.lib().zxv_paradox_log_count(self._handle(), int(which), out))
        return int(out[0])

    def log_head(self, which: ParadoxLog) -> bytes:
        """The chain head: the last record's hash (32 zeros when empty)."""
        out = ffi.new("uint8_t[32]")
        self._check(_native.lib().zxv_paradox_log_head(self._handle(), int(which), out))
        return bytes(ffi.buffer(out, 32))

    def log_record(self, which: ParadoxLog, index: int) -> bytes:
        """Record ``index`` of a log as bytes (:data:`PDX_REC_BYTES`, or
        :data:`PDX_QREC_BYTES` for the quarantine log; layout in ``zxv_api.h``)."""
        h = self._handle()
        w, i = int(which), _u32(index, "index")
        return _native.read_bytes(lambda b, c, n: _native.lib().zxv_paradox_log_record(h, w, i, b, c, n),
                                  self._context, PDX_REC_BYTES)

    def log_records(self, which: ParadoxLog) -> Iterator[bytes]:
        """Every record of a log, oldest first."""
        for i in range(self.log_count(which)):
            yield self.log_record(which, i)

    def verify_log(self, which: ParadoxLog) -> None:
        """Recompute every link of a log.

        Raises:
            ZxvFatal: a record or the head no longer matches (the context is
                poisoned: only a memory fault in this process can alter a log).
        """
        self._check(_native.lib().zxv_paradox_log_verify(self._handle(), int(which)))

    # ------------------------------------------------------------ operators

    def constructive(self, policy: ParadoxPolicy, pair: Classification, synth: Optional[Sequence[int]], *,
                     tension: int, cap: Optional[Capability] = None) -> ParadoxOutcome:
        """One evaluation, then (with ``synth``) one proposed update of the policy.

        ``synth`` is your synthesis; this checks and applies it: forbidden ->
        ROLLED_BACK; kind not CONSTRUCTIVE -> REFUSED; an armed update whose
        tension grew -> previous values, ROLLED_BACK; ``synth`` None -> KEPT;
        flagged policy or any value outside its bounds -> PROPOSAL_FOR_OWNER
        (nothing changes); else APPLIED. ``tension`` is your integer measure
        of the paradox (higher is worse).

        Raises:
            ZxvUsageError: ``ZXV_E_ARG`` (a bad pair, a length mismatch).
            ZxvRefusedError: ``ZXV_E_CAPACITY`` (operator log full: halt).
        """
        k, t = pair._k()
        n = len(policy)
        s: Any = ffi.NULL
        if synth is not None:
            if len(synth) != n:
                raise _native.make_error(-1, f"synth must have {n} values")
            s = _i64s(synth, "synth")
        out = ffi.new("int32_t *")
        with policy._lock:
            self._check(_native.lib().zxv_paradox_constructive(self._c(cap), self._handle(), policy._handle(), k, t,
                                                               s, n, _u32(tension, "tension"), out))
        return ParadoxOutcome(out[0])

    def destructive(self, pair: Classification, *, state: bytearray, checkpoint: bytes, steps: int = 0,
                    step_limit: int = 0, spent: int = 0, budget: int = 0, trace: Sequence[int] = (),
                    invariant_ok: bool = True, pattern: bytes = b"", submitter: int = 0,
                    cap: Optional[Capability] = None) -> DestructiveResult:
        """Detect and contain.

        The checks: ``steps >= step_limit`` (0: no limit), ``spent >= budget``
        (0: no budget), a repeated fingerprint in ``trace``, ``invariant_ok``
        False, and a forbidden tag. When the kind is DESTRUCTIVE or any check
        fires: the work is quarantined, ``state`` is restored IN PLACE from
        ``checkpoint`` and a :class:`RepairProof` is returned (CONTAINED).
        Otherwise the outcome is REFUSED and nothing changes.

        It contains; it has no way to reply to or act against the submitter.

        Raises:
            ZxvUsageError: ``ZXV_E_ARG`` (a bad pair, mismatched lengths),
                ``ZXV_E_RANGE`` (trace over :data:`PDX_TRACE_MAX`).
            ZxvRefusedError: ``ZXV_E_CAPACITY`` (a log is full: halt). The state
                may already be restored then (rollback is always safe).
        """
        if not isinstance(state, bytearray):
            raise TypeError("state must be a bytearray (it is restored in place)")
        ck = _native.to_bytes(checkpoint, "checkpoint")
        if len(ck) != len(state):
            raise _native.make_error(-1, "checkpoint must be as long as state")
        k, t = pair._k()
        tr = ffi.new("uint64_t[]", [_native.exact_int(x, "trace", *_native.U64) for x in trace]) if trace else ffi.NULL
        pat = _native.to_bytes(pattern, "pattern")
        sb = ffi.from_buffer("uint8_t[]", state, require_writable=True) if state else ffi.NULL
        out, proof = ffi.new("int32_t *"), ffi.new("uint8_t[]", PDX_REPAIR_BYTES)
        self._check(_native.lib().zxv_paradox_destructive(
            self._c(cap), self._handle(), k, t, _u32(steps, "steps"), _u32(step_limit, "step_limit"),
            _u32(spent, "spent"), _u32(budget, "budget"), tr, len(trace), 1 if invariant_ok else 0, sb, _b(ck),
            len(ck), _b(pat), len(pat), _u32(submitter, "submitter"), out, proof))
        oc = ParadoxOutcome(out[0])
        rp = RepairProof.parse(bytes(ffi.buffer(proof, PDX_REPAIR_BYTES))) if oc is ParadoxOutcome.CONTAINED else None
        return DestructiveResult(oc, rp)

    def neutral(self, hold: ParadoxHold, pair: Classification, *, owner_pick: Optional[int] = None,
                cap: Optional[Capability] = None) -> NeutralResult:
        """Settle on decisive evidence or on the owner's pick (a branch index);
        otherwise keep every branch. Outcomes: SETTLED, DEFERRED, ROLLED_BACK
        (forbidden), REFUSED (kind not NEUTRAL).

        Raises:
            ZxvUsageError: ``ZXV_E_NOT_FOUND`` (``owner_pick`` past the branches).
        """
        k, t = pair._k()
        pick = -1 if owner_pick is None else _native.exact_int(owner_pick, "owner_pick", 0, 2**31 - 1)
        out, st = ffi.new("int32_t *"), ffi.new("int32_t *")
        with hold._lock:
            self._check(_native.lib().zxv_paradox_neutral(self._c(cap), self._handle(), hold._handle(), k, t, pick,
                                                          out, st))
        return NeutralResult(ParadoxOutcome(out[0]), None if st[0] < 0 else int(st[0]))

    def special(self, pair: Classification, signature: bytes, subject: bytes, *,
                cap: Optional[Capability] = None) -> SpecialResult:
        """Record an unclassified paradox: a permanent frontier record (never
        edited or erased) plus an operator record. The work stays sandboxed
        until a reviewed classification exists. Outcomes: RECORDED, REFUSED
        (kind not SPECIAL), ROLLED_BACK (forbidden)."""
        k, t = pair._k()
        out, rec = ffi.new("int32_t *"), ffi.new("uint32_t *")
        self._check(_native.lib().zxv_paradox_special(self._c(cap), self._handle(), k, t,
                                                      _b(_h32(signature, "signature")), _b(_h32(subject, "subject")),
                                                      out, rec))
        oc = ParadoxOutcome(out[0])
        return SpecialResult(oc, int(rec[0]) if oc is ParadoxOutcome.RECORDED else None)

    def draft(self, record: int, kind: ParadoxKind, *, reason: int = 0, flags: ParadoxFlag = ParadoxFlag.NONE,
              cap: Optional[Capability] = None) -> bytes:
        """A draft classification of frontier record ``record`` for review
        (:data:`PDX_DRAFT_BYTES` bytes; logged).

        Raises:
            ZxvUsageError: ``ZXV_E_NOT_FOUND`` (no such record), ``ZXV_E_ARG``.
        """
        out = ffi.new("uint8_t[]", PDX_DRAFT_BYTES)
        self._check(_native.lib().zxv_paradox_draft(self._c(cap), self._handle(), _u32(record, "record"),
                                                    _native.exact_int(int(kind), "kind", *_native.I32),
                                                    _native.exact_int(reason, "reason", 0, 255),
                                                    _native.exact_int(int(flags), "flags", 0, 255), out))
        return bytes(ffi.buffer(out, PDX_DRAFT_BYTES))

    def review(self, draft: bytes, *, owner_ok: bool = False, consensus_ok: bool = False,
               cap: Optional[Capability] = None) -> ParadoxOutcome:
        """Review a draft (ours, or a peer's matching this book's frontier record).

        A MONEY or DEVICE draft needs ``owner_ok``; others ``owner_ok`` or
        ``consensus_ok`` (your quorum result). Outcomes: ACCEPTED (a new
        registry entry), PROPOSAL_FOR_OWNER (not approved), REFUSED (already
        registered, or the registry is full).

        Raises:
            VerificationFailedError: the draft's hash or signature does not match the record.
            ZxvUsageError: ``ZXV_E_FORMAT``, ``ZXV_E_NOT_FOUND``.
        """
        d = _native.fixed_bytes(draft, PDX_DRAFT_BYTES, "draft")
        out = ffi.new("int32_t *")
        self._check(_native.lib().zxv_paradox_review(self._c(cap), self._handle(), _b(d), 1 if owner_ok else 0,
                                                     1 if consensus_ok else 0, out))
        return ParadoxOutcome(out[0])

    def graph_pass(self, graph: ParadoxGraph, *, cap: Optional[Capability] = None) -> ParadoxOutcome:
        """One pass over a graph, in the fixed order (contain, sandbox, hold, apply).
        Logged. Returns RESOLVED (every node final) or PENDING.

        Raises:
            ParadoxStepLimitError: the pass count reached the step limit unresolved;
                the pass WAS logged and the graph stays readable for the owner.
            ZxvUsageError: ``ZXV_E_NOT_FOUND`` (a dependency names no node).
        """
        out = ffi.new("int32_t *")
        with graph._lock:
            self._check(_native.lib().zxv_paradox_graph_pass(self._c(cap), self._handle(), graph._handle(), out))
        return ParadoxOutcome(out[0])
