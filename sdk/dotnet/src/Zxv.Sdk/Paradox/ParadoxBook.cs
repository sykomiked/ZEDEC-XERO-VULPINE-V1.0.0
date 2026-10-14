// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using Zxv.Interop;

namespace Zxv.Paradox;

/// <summary>What a containment returned.</summary>
/// <param name="Outcome"><see cref="ParadoxOutcome.Contained"/>, or <see cref="ParadoxOutcome.Refused"/> when nothing fired and the kind is not destructive.</param>
/// <param name="Proof">The RepairProof when contained; <c>default</c> otherwise.</param>
public readonly record struct ParadoxContainment(ParadoxOutcome Outcome, RepairProof Proof);

/// <summary>What a neutral step returned.</summary>
/// <param name="Outcome">Settled, Deferred, RolledBack or Refused.</param>
/// <param name="SettledBranch">The settled branch, or -1.</param>
public readonly record struct ParadoxHoldResult(ParadoxOutcome Outcome, int SettledBranch);

/// <summary>What recording a special paradox returned.</summary>
/// <param name="Outcome">Recorded, Refused or RolledBack.</param>
/// <param name="FrontierRecord">The frontier record index, or -1 when not recorded.</param>
public readonly record struct ParadoxFrontierResult(ParadoxOutcome Outcome, long FrontierRecord);

/// <summary>
/// The paradox book (ABI 1.5, <c>zxv_paradox</c>): the operator log, the
/// frontier log, the quarantine log and the signature registry, plus the
/// four operators that write them. Create one with
/// <see cref="ZxvPlatform.CreateParadoxBook(Capability, int, int)"/>.
/// </summary>
/// <remarks>
/// <para>
/// An operator that ran has appended one operator record and returns its
/// decision as a <see cref="ParadoxOutcome"/>. An exception means it did not
/// run: nothing was logged and nothing changed (two exceptions:
/// <see cref="Destructive"/> restores the state even when the logs are full,
/// and <see cref="Pass"/> logs the pass that reached the step limit). A full
/// log is <see cref="ZxvStatus.CapacityExceeded"/>: halt the platform, since
/// work that cannot be recorded must not continue. Everything that writes
/// needs <see cref="CapabilityRights.Paradox"/>; classifying and reading need none.
/// The book is thread-safe.
/// </para>
/// <para>
/// HONEST LIMITS. Automatic changes happen only inside published bounds;
/// anything touching money or devices goes to the owner, and "owner
/// approved" is the caller's assertion (inside one process this library
/// cannot check who the owner is). Destructive work is contained, never
/// answered: there is no way to reply to, or act against, a sender.
/// Detection is only by the checks the caller runs. Nothing here signs; sign
/// proofs and records with <see cref="Ed25519"/> or <see cref="MlDsa87"/>.
/// </para>
/// </remarks>
public sealed class ParadoxBook : IDisposable
{
    /// <summary>The source of a registry entry that was not learned from the frontier (<c>ZXV_PDX_SRC_SEED</c>).</summary>
    public const ulong SeedSource = ulong.MaxValue;

    /// <summary>Records per log (<c>ZXV_PDX_LOG_MAX</c>).</summary>
    public const int MaxLogRecords = 65536;

    /// <summary>Registry entries (<c>ZXV_PDX_REGISTRY_MAX</c>).</summary>
    public const int MaxRegistryEntries = 65536;

    /// <summary>Operator and frontier record length (<c>ZXV_PDX_REC_BYTES</c>).</summary>
    public const int RecordBytes = 144;

    /// <summary>Quarantine record length (<c>ZXV_PDX_QREC_BYTES</c>).</summary>
    public const int QuarantineRecordBytes = 110;

    /// <summary>Most state fingerprints per detection (<c>ZXV_PDX_TRACE_MAX</c>).</summary>
    public const int MaxTrace = 4096;

    private readonly Capability _cap;
    private readonly ZxvParadoxHandle _handle;

    internal ParadoxBook(Capability capability, int logCapacity, int registryCapacity)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(logCapacity);
        ArgumentOutOfRangeException.ThrowIfNegative(registryCapacity);
        _cap = capability;
        capability.Enter("ParadoxBook.Create");
        int rc = NativeMethods.ParadoxCreate(capability.Handle, (uint)logCapacity, (uint)registryCapacity, out _handle);
        if (rc != (int)ZxvStatus.Ok)
        {
            _handle.Dispose();
            capability.Check(rc, "ParadoxBook.Create");
        }

        _handle.AddDependency(capability.Context.Handle);
    }

    /// <summary>Gets the number of registry entries.</summary>
    public int RegistryCount
    {
        get
        {
            Read("ParadoxBook.RegistryCount");
            Check(NativeMethods.ParadoxRegistryCount(_handle, out uint n), "ParadoxBook.RegistryCount");
            return (int)n;
        }
    }

    /// <summary>
    /// Classifies a tag, with a registry lookup: a known signature overrides
    /// the trend for Both and Neither. Reads only.
    /// </summary>
    /// <param name="tag">The tag.</param>
    /// <param name="evidence">Counts before and after, or <see langword="null"/> for no trend (polarity neutral).</param>
    /// <param name="stale">The newest evidence is past the caller's freshness limit (ignored without evidence).</param>
    /// <param name="signature">The anomaly's 32-byte signature, or empty.</param>
    /// <returns>The class pair.</returns>
    public ParadoxClassification Classify(Hexagram tag, ParadoxEvidence? evidence = null, bool stale = false, ReadOnlySpan<byte> signature = default)
    {
        Read("ParadoxBook.Classify");
        bool added = false;
        try
        {
            _handle.DangerousAddRef(ref added);
            return ClassifyCore(_handle.DangerousGetHandle(), tag, evidence, stale, signature, _cap.Context);
        }
        finally
        {
            if (added)
            {
                _handle.DangerousRelease();
            }
        }
    }

    /// <summary>
    /// Adds a reviewed signature to the registry (append-only) as
    /// constructive, destructive or neutral. Needs <see cref="CapabilityRights.Paradox"/>.
    /// </summary>
    /// <param name="signature">32 bytes.</param>
    /// <param name="kind">Constructive, Destructive or Neutral.</param>
    /// <param name="reason">The caller's reason code.</param>
    /// <param name="concerns">Money or device flags.</param>
    /// <param name="ownerApproved">The caller's assertion that the owner approved (required with concerns).</param>
    /// <returns>The entry's index.</returns>
    /// <exception cref="ZxvRefusedException">Concerns without owner approval (<see cref="ZxvStatus.ParadoxOwnerApproval"/>).</exception>
    /// <exception cref="ZxvUsageException">Already registered (<see cref="ZxvStatus.Duplicate"/>), a bad kind, or full.</exception>
    public unsafe int RegistryAdd(ReadOnlySpan<byte> signature, ParadoxKind kind, byte reason, ParadoxConcerns concerns, bool ownerApproved)
    {
        NativeBuffers.RequireLength(signature, 32, nameof(signature));
        Write("ParadoxBook.RegistryAdd");
        fixed (byte* s = signature)
        {
            Check(NativeMethods.ParadoxRegistryAdd(_cap.Handle, _handle, s, (int)kind, reason, (byte)concerns, ownerApproved ? 1 : 0, out uint index), "ParadoxBook.RegistryAdd");
            return (int)index;
        }
    }

    /// <summary>The index of a signature in the registry, or -1.</summary>
    /// <param name="signature">32 bytes.</param>
    /// <returns>The index or -1.</returns>
    public unsafe int RegistryFind(ReadOnlySpan<byte> signature)
    {
        NativeBuffers.RequireLength(signature, 32, nameof(signature));
        Read("ParadoxBook.RegistryFind");
        fixed (byte* s = signature)
        {
            int rc = NativeMethods.ParadoxRegistryFind(_handle, s, out uint index);
            if (rc == (int)ZxvStatus.NotFound)
            {
                return -1;
            }

            Check(rc, "ParadoxBook.RegistryFind");
            return (int)index;
        }
    }

    /// <summary>Registry entry <paramref name="index"/>.</summary>
    /// <param name="index">0-based.</param>
    /// <returns>The entry.</returns>
    /// <exception cref="ZxvUsageException">No such entry (<see cref="ZxvStatus.NotFound"/>).</exception>
    public unsafe ParadoxRegistryEntry RegistryAt(int index)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        Read("ParadoxBook.RegistryAt");
        var sig = new byte[32];
        fixed (byte* s = sig)
        {
            Check(NativeMethods.ParadoxRegistryAt(_handle, (uint)index, s, out int kind, out byte reason, out byte flags, out ulong src), "ParadoxBook.RegistryAt");
            return new ParadoxRegistryEntry(sig, (ParadoxKind)kind, reason, (ParadoxConcerns)flags, src);
        }
    }

    /// <summary>The number of records in a log.</summary>
    /// <param name="log">The log.</param>
    /// <returns>The count.</returns>
    public int LogCount(ParadoxLog log)
    {
        Read("ParadoxBook.LogCount");
        Check(NativeMethods.ParadoxLogCount(_handle, (int)log, out uint n), "ParadoxBook.LogCount");
        return (int)n;
    }

    /// <summary>A log's chain head: the last record's hash (32 zeros when empty).</summary>
    /// <param name="log">The log.</param>
    /// <returns>32 bytes.</returns>
    public unsafe byte[] LogHead(ParadoxLog log)
    {
        Read("ParadoxBook.LogHead");
        var head = new byte[32];
        fixed (byte* h = head)
        {
            Check(NativeMethods.ParadoxLogHead(_handle, (int)log, h), "ParadoxBook.LogHead");
        }

        return head;
    }

    /// <summary>
    /// Record <paramref name="index"/> of a log, as bytes: <see cref="RecordBytes"/>
    /// for the operator and frontier logs, <see cref="QuarantineRecordBytes"/>
    /// for the quarantine log. Each is SHA-256 hash-chained, so a host can recompute it.
    /// </summary>
    /// <param name="log">The log.</param>
    /// <param name="index">0-based.</param>
    /// <returns>The record.</returns>
    public unsafe byte[] LogRecord(ParadoxLog log, int index)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        Read("ParadoxBook.LogRecord");
        return NativeBuffers.ReadBytes((byte* b, nuint c, out nuint l) => NativeMethods.ParadoxLogRecord(_handle, (int)log, (uint)index, b, c, out l), rc => Check(rc, "ParadoxBook.LogRecord"));
    }

    /// <summary>
    /// Recomputes every link of a log. A mismatch poisons the platform
    /// (<see cref="ZxvFatalException"/>): only a memory fault in this process
    /// can alter a log.
    /// </summary>
    /// <param name="log">The log.</param>
    public void VerifyLog(ParadoxLog log)
    {
        Read("ParadoxBook.VerifyLog");
        Check(NativeMethods.ParadoxLogVerify(_handle, (int)log), "ParadoxBook.VerifyLog");
    }

    /// <summary>
    /// One constructive evaluation, then (when <paramref name="proposed"/> is
    /// given) one proposed update: applied only when every value is inside its
    /// bounds and the policy has no concerns; otherwise a proposal for the
    /// owner. An armed update is rolled back if tension grew. Needs <see cref="CapabilityRights.Paradox"/>.
    /// </summary>
    /// <param name="policy">The policy (same platform).</param>
    /// <param name="kind">The classified kind.</param>
    /// <param name="tag">The classified tag.</param>
    /// <param name="proposed">The proposed values (the caller's synthesis), or empty to evaluate only.</param>
    /// <param name="tension">The caller's integer measure of the paradox (higher is worse).</param>
    /// <returns>The outcome.</returns>
    public unsafe ParadoxOutcome Constructive(ParadoxPolicy policy, ParadoxKind kind, Hexagram tag, ReadOnlySpan<long> proposed, uint tension)
    {
        ArgumentNullException.ThrowIfNull(policy);
        if (!proposed.IsEmpty)
        {
            policy.RequireCount(proposed, nameof(proposed));
        }

        lock (policy.Lock)
        {
            policy.EnsureOpen();
            Write("ParadoxBook.Constructive");
            fixed (long* s = proposed)
            {
                Check(NativeMethods.ParadoxConstructive(_cap.Handle, _handle, policy.Handle, (int)kind, tag.Tag, proposed.IsEmpty ? null : s, (uint)policy.Count, tension, out int outcome), "ParadoxBook.Constructive");
                return (ParadoxOutcome)outcome;
            }
        }
    }

    /// <summary>
    /// Detects and contains. When the kind is destructive or any check fires
    /// (step limit, budget, a repeated fingerprint, a failed invariant, a
    /// forbidden tag), the work is quarantined, <paramref name="state"/> is
    /// restored from <paramref name="checkpoint"/> in place, and a
    /// RepairProof is returned. Otherwise the refusal is logged and nothing
    /// changes. Needs <see cref="CapabilityRights.Paradox"/>.
    /// </summary>
    /// <param name="kind">The classified kind.</param>
    /// <param name="tag">The classified tag.</param>
    /// <param name="steps">Steps taken.</param>
    /// <param name="stepLimit">The step limit (0: none).</param>
    /// <param name="spent">Budget spent.</param>
    /// <param name="budget">The budget (0: none).</param>
    /// <param name="trace">State fingerprints in visit order (at most <see cref="MaxTrace"/>).</param>
    /// <param name="invariantHolds">The caller's invariant check result.</param>
    /// <param name="state">The live state; restored in place when contained.</param>
    /// <param name="checkpoint">The checkpoint, the same length as <paramref name="state"/>.</param>
    /// <param name="pattern">The canonical description of the trap (its signature is shared with peers).</param>
    /// <param name="submitter">Who submitted the work.</param>
    /// <returns>The outcome and, when contained, the proof.</returns>
    public unsafe ParadoxContainment Destructive(ParadoxKind kind, Hexagram tag, uint steps, uint stepLimit, uint spent, uint budget, ReadOnlySpan<ulong> trace, bool invariantHolds, Span<byte> state, ReadOnlySpan<byte> checkpoint, ReadOnlySpan<byte> pattern, uint submitter)
    {
        if (checkpoint.Length != state.Length)
        {
            throw new ArgumentException("The checkpoint must be as long as the state.", nameof(checkpoint));
        }

        Write("ParadoxBook.Destructive");
        var proof = new byte[RepairProof.Length];
        fixed (ulong* t = trace)
        fixed (byte* s = state)
        fixed (byte* c = checkpoint)
        fixed (byte* p = pattern)
        fixed (byte* o = proof)
        {
            Check(
                NativeMethods.ParadoxDestructive(_cap.Handle, _handle, (int)kind, tag.Tag, steps, stepLimit, spent, budget, t, (uint)trace.Length, invariantHolds ? 1 : 0, s, c, (uint)state.Length, p, (uint)pattern.Length, submitter, out int outcome, o),
                "ParadoxBook.Destructive");
            var result = (ParadoxOutcome)outcome;
            return new ParadoxContainment(result, result == ParadoxOutcome.Contained ? RepairProof.Wrap(proof) : default);
        }
    }

    /// <summary>
    /// Settles a hold on decisive evidence or the owner's pick; otherwise
    /// keeps every branch. Needs <see cref="CapabilityRights.Paradox"/>.
    /// </summary>
    /// <param name="hold">The hold (same platform).</param>
    /// <param name="kind">The classified kind.</param>
    /// <param name="tag">The classified tag.</param>
    /// <param name="ownerPick">A branch index the owner picked, or -1.</param>
    /// <returns>The outcome and the settled branch.</returns>
    public ParadoxHoldResult Neutral(ParadoxHold hold, ParadoxKind kind, Hexagram tag, int ownerPick = -1)
    {
        ArgumentNullException.ThrowIfNull(hold);
        lock (hold.Lock)
        {
            hold.EnsureOpen();
            Write("ParadoxBook.Neutral");
            Check(NativeMethods.ParadoxNeutral(_cap.Handle, _handle, hold.Handle, (int)kind, tag.Tag, ownerPick, out int outcome, out int settled), "ParadoxBook.Neutral");
            return new ParadoxHoldResult((ParadoxOutcome)outcome, settled);
        }
    }

    /// <summary>
    /// Records an unclassified paradox: a permanent frontier record that is
    /// never edited or erased. The work stays sandboxed (not run) until a
    /// reviewed classification exists. Needs <see cref="CapabilityRights.Paradox"/>.
    /// </summary>
    /// <param name="kind">The classified kind.</param>
    /// <param name="tag">The classified tag.</param>
    /// <param name="signature">The anomaly's 32-byte signature.</param>
    /// <param name="subject">The work's 32-byte digest.</param>
    /// <returns>The outcome and the frontier record index.</returns>
    public unsafe ParadoxFrontierResult Special(ParadoxKind kind, Hexagram tag, ReadOnlySpan<byte> signature, ReadOnlySpan<byte> subject)
    {
        NativeBuffers.RequireLength(signature, 32, nameof(signature));
        NativeBuffers.RequireLength(subject, 32, nameof(subject));
        Write("ParadoxBook.Special");
        fixed (byte* s = signature)
        fixed (byte* j = subject)
        {
            Check(NativeMethods.ParadoxSpecial(_cap.Handle, _handle, (int)kind, tag.Tag, s, j, out int outcome, out uint rec), "ParadoxBook.Special");
            return new ParadoxFrontierResult((ParadoxOutcome)outcome, rec == uint.MaxValue ? -1 : rec);
        }
    }

    /// <summary>A draft classification of frontier record <paramref name="record"/>, for review. Needs <see cref="CapabilityRights.Paradox"/>.</summary>
    /// <param name="record">The frontier record index.</param>
    /// <param name="kind">Constructive, Destructive or Neutral.</param>
    /// <param name="reason">The caller's reason code.</param>
    /// <param name="concerns">Money or device flags.</param>
    /// <returns>The draft.</returns>
    public unsafe ParadoxDraft Draft(long record, ParadoxKind kind, byte reason, ParadoxConcerns concerns)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(record);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(record, uint.MaxValue - 1L);
        Write("ParadoxBook.Draft");
        var draft = new byte[ParadoxDraft.Length];
        fixed (byte* d = draft)
        {
            Check(NativeMethods.ParadoxDraft(_cap.Handle, _handle, (uint)record, (int)kind, reason, (byte)concerns, d), "ParadoxBook.Draft");
        }

        return ParadoxDraft.Wrap(draft);
    }

    /// <summary>
    /// Reviews a draft (ours, or one a peer sent: it must match this book's
    /// frontier record). A draft with concerns needs the owner; others the
    /// owner or the caller's quorum. Needs <see cref="CapabilityRights.Paradox"/>.
    /// </summary>
    /// <param name="draft">The draft.</param>
    /// <param name="ownerApproved">The caller's assertion that the owner approved.</param>
    /// <param name="consensusApproved">The caller's quorum result.</param>
    /// <returns>Accepted, ProposalForOwner or Refused.</returns>
    /// <exception cref="VerificationFailedException">The hash or signature does not match the record.</exception>
    public unsafe ParadoxOutcome Review(ParadoxDraft draft, bool ownerApproved, bool consensusApproved)
    {
        byte[] d = draft.RequireBytes();
        Write("ParadoxBook.Review");
        fixed (byte* p = d)
        {
            Check(NativeMethods.ParadoxReview(_cap.Handle, _handle, p, ownerApproved ? 1 : 0, consensusApproved ? 1 : 0, out int outcome), "ParadoxBook.Review");
            return (ParadoxOutcome)outcome;
        }
    }

    /// <summary>
    /// Runs one pass of a graph in the fixed order (contain, sandbox, hold,
    /// apply). Needs <see cref="CapabilityRights.Paradox"/>.
    /// </summary>
    /// <param name="graph">The graph (same platform).</param>
    /// <returns><see cref="ParadoxOutcome.Resolved"/> or <see cref="ParadoxOutcome.Pending"/>.</returns>
    /// <exception cref="ZxvRefusedException">
    /// The pass count reached the step limit unresolved
    /// (<see cref="ZxvStatus.ParadoxStepLimit"/>): the pass was logged, the
    /// graph stays readable, and it goes to the owner.
    /// </exception>
    public ParadoxOutcome Pass(ParadoxGraph graph)
    {
        ArgumentNullException.ThrowIfNull(graph);
        lock (graph.Lock)
        {
            graph.EnsureOpen();
            Write("ParadoxBook.Pass");
            Check(NativeMethods.ParadoxGraphPass(_cap.Handle, _handle, graph.Handle, out int outcome), "ParadoxBook.Pass");
            return (ParadoxOutcome)outcome;
        }
    }

    /// <summary>Releases the book.</summary>
    public void Dispose() => _handle.Dispose();

    internal static unsafe ParadoxClassification ClassifyCore(IntPtr book, Hexagram tag, ParadoxEvidence? evidence, bool stale, ReadOnlySpan<byte> signature, ContextRef? context)
    {
        if (!signature.IsEmpty)
        {
            NativeBuffers.RequireLength(signature, 32, nameof(signature));
        }

        uint* ev = stackalloc uint[4];
        if (evidence is { } e)
        {
            ev[0] = e.PositiveBefore;
            ev[1] = e.NegativeBefore;
            ev[2] = e.PositiveAfter;
            ev[3] = e.NegativeAfter;
        }

        fixed (byte* s = signature)
        {
            int rc = NativeMethods.ParadoxClassify(book, tag.Tag, evidence is null ? null : ev, stale ? 1 : 0, signature.IsEmpty ? null : s, out int kind, out byte outTag, out int reg);
            if (rc != (int)ZxvStatus.Ok)
            {
                if (context is null)
                {
                    NativeCall.Check(rc, "Paradox.Classify");
                }
                else
                {
                    NativeCall.Check(rc, "Paradox.Classify", context);
                }
            }

            return new ParadoxClassification((ParadoxKind)kind, Hexagram.FromTag(outTag), reg);
        }
    }

    private void Write(string operation)
    {
        ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
        _cap.Enter(operation);
    }

    private void Read(string operation)
    {
        ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
        _cap.Context.ThrowIfHalted(operation);
    }

    private void Check(int rc, string operation)
    {
        if (rc != (int)ZxvStatus.Ok)
        {
            NativeCall.Check(rc, operation, _cap.Context);
        }
    }
}
