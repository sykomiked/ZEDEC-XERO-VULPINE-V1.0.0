// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Buffers.Binary;
using System.Runtime.InteropServices;
using Zxv.Interop;

namespace Zxv.Paradox;

/// <summary>The four paradox kinds, and None (<c>ZXV_PDX_KIND_*</c>).</summary>
public enum ParadoxKind
{
    /// <summary>Both, evidence margin growing: a bounded parameter update (catalyst).</summary>
    Constructive = 0,

    /// <summary>Both, margin shrinking or stale: contain, quarantine, roll back (immune).</summary>
    Destructive = 1,

    /// <summary>Both and steady past the potential phase, or Neither: hold every branch.</summary>
    Neutral = 2,

    /// <summary>Both, steady, potential phase, no registry signature: the frontier.</summary>
    Special = 3,

    /// <summary>True or False: not a paradox.</summary>
    None = 4,
}

/// <summary>What to do with a classified paradox (<c>ZXV_PDX_DO_*</c>).</summary>
public enum ParadoxAction
{
    /// <summary>Not a paradox: use <see cref="Hexagram.Action"/>.</summary>
    None = 0,

    /// <summary>Constructive: <see cref="ParadoxBook.Constructive"/>.</summary>
    Synthesize = 1,

    /// <summary>Destructive: <see cref="ParadoxBook.Destructive"/>.</summary>
    Contain = 2,

    /// <summary>Neutral: <see cref="ParadoxBook.Neutral"/>.</summary>
    Hold = 3,

    /// <summary>Special: <see cref="ParadoxBook.Special"/> (sandbox).</summary>
    Record = 4,

    /// <summary>Forbidden: refuse, roll back, quarantine.</summary>
    Rollback = 5,
}

/// <summary>Operator outcomes (<c>ZXV_PDX_OUT_*</c>). REFUSED and PROPOSAL_FOR_OWNER are decisions, not errors.</summary>
public enum ParadoxOutcome
{
    /// <summary>A constructive update was applied.</summary>
    Applied = 0,

    /// <summary>An applied update survived evaluation.</summary>
    Kept = 1,

    /// <summary>Out of bounds or unapproved: nothing changed; the owner decides.</summary>
    ProposalForOwner = 2,

    /// <summary>Tension grew, or forbidden: restored.</summary>
    RolledBack = 3,

    /// <summary>Destructive: quarantined and rolled back.</summary>
    Contained = 4,

    /// <summary>Neutral: still holding every branch.</summary>
    Deferred = 5,

    /// <summary>Neutral: one branch chosen.</summary>
    Settled = 6,

    /// <summary>Special: a frontier record was written.</summary>
    Recorded = 7,

    /// <summary>A reviewed draft joined the registry.</summary>
    Accepted = 8,

    /// <summary>Graph: more passes needed.</summary>
    Pending = 9,

    /// <summary>Graph: every node is final.</summary>
    Resolved = 10,

    /// <summary>Graph: the step limit was reached (reported as <see cref="ZxvStatus.ParadoxStepLimit"/>).</summary>
    EscalateToOwner = 11,

    /// <summary>Wrong kind: nothing changed.</summary>
    Refused = 12,

    /// <summary>Never reported: a full log is <see cref="ZxvStatus.CapacityExceeded"/>.</summary>
    LogFull = 13,
}

/// <summary>Graph node states (<c>ZXV_PDX_NS_*</c>). Final: Clear, Contained, RolledBack, Tainted, Applied.</summary>
public enum ParadoxNodeState
{
    /// <summary>Not resolved yet.</summary>
    Open = 0,

    /// <summary>Kind None: not a paradox.</summary>
    Clear = 1,

    /// <summary>Destructive: contained (sticky).</summary>
    Contained = 2,

    /// <summary>Forbidden: refused and rolled back (sticky).</summary>
    RolledBack = 3,

    /// <summary>Depends on contained work (sticky).</summary>
    Tainted = 4,

    /// <summary>Special: recorded, awaiting review.</summary>
    Sandboxed = 5,

    /// <summary>Depends on a sandboxed node.</summary>
    Waiting = 6,

    /// <summary>Neutral: branches kept.</summary>
    Held = 7,

    /// <summary>Constructive reached by held or waiting work.</summary>
    Blocked = 8,

    /// <summary>Constructive: applied.</summary>
    Applied = 9,
}

/// <summary>What a policy, registry entry or draft touches (<c>ZXV_PDX_FLAG_*</c>); either means the owner decides.</summary>
[Flags]
public enum ParadoxConcerns : byte
{
    /// <summary>Nothing that needs the owner.</summary>
    None = 0,

    /// <summary>Touches money.</summary>
    Money = 1,

    /// <summary>Touches devices.</summary>
    Device = 2,
}

/// <summary>Which checks fired (<c>ZXV_PDX_TRIP_*</c>).</summary>
[Flags]
public enum ParadoxTrips : uint
{
    /// <summary>None.</summary>
    None = 0,

    /// <summary>steps &gt;= step limit.</summary>
    Steps = 1,

    /// <summary>spent &gt;= budget.</summary>
    Budget = 2,

    /// <summary>A state fingerprint repeated.</summary>
    Cycle = 4,

    /// <summary>The caller's invariant result was false.</summary>
    Invariant = 8,

    /// <summary>The tag was already forbidden.</summary>
    Forbidden = 16,
}

/// <summary>The book's three logs (<c>ZXV_PDX_LOG_*</c>).</summary>
public enum ParadoxLog
{
    /// <summary>The operator log.</summary>
    Operator = 0,

    /// <summary>Special records, never edited.</summary>
    Frontier = 1,

    /// <summary>Contained work (the lpres_hex X6 log).</summary>
    Quarantine = 2,
}

/// <summary>Evidence counts before and after, for classification.</summary>
/// <param name="PositiveBefore">For, before.</param>
/// <param name="NegativeBefore">Against, before.</param>
/// <param name="PositiveAfter">For, after.</param>
/// <param name="NegativeAfter">Against, after.</param>
public readonly record struct ParadoxEvidence(uint PositiveBefore, uint NegativeBefore, uint PositiveAfter, uint NegativeAfter);

/// <summary>The result of classifying a tag: the class pair every operator takes.</summary>
/// <param name="Kind">The kind.</param>
/// <param name="Tag">The tag with the kind's polarity (base and phase kept; forbidden stays forbidden).</param>
/// <param name="RegistryIndex">The registry entry that decided it, or -1.</param>
public readonly record struct ParadoxClassification(ParadoxKind Kind, Hexagram Tag, int RegistryIndex);

/// <summary>A registry entry.</summary>
/// <param name="Signature">The 32-byte signature.</param>
/// <param name="Kind">Its kind.</param>
/// <param name="Reason">The caller's reason code.</param>
/// <param name="Concerns">Money or device flags.</param>
/// <param name="Source">The frontier record it was learned from, or <see cref="ParadoxBook.SeedSource"/>.</param>
public sealed record ParadoxRegistryEntry(byte[] Signature, ParadoxKind Kind, byte Reason, ParadoxConcerns Concerns, ulong Source);

/// <summary>
/// A RepairProof (<see cref="Length"/> bytes, little-endian): what was
/// contained, the state hashes before and after rollback, the signature to
/// share with peers and a binding hash. A value type over the bytes.
/// </summary>
/// <remarks><see cref="Verify"/> checks integrity only; signing the proof, and checking that signature, is the caller's job.</remarks>
public readonly struct RepairProof : IEquatable<RepairProof>
{
    /// <summary>The proof length (<c>ZXV_PDX_REPAIR_BYTES</c>).</summary>
    public const int Length = 160;

    private readonly byte[]? _bytes;

    private RepairProof(byte[] bytes)
    {
        _bytes = bytes;
    }

    /// <summary>Gets the bytes (empty for <c>default</c>).</summary>
    public ReadOnlySpan<byte> Bytes => _bytes;

    /// <summary>Gets the checks that fired.</summary>
    public ParadoxTrips Trips => (ParadoxTrips)U32(0);

    /// <summary>Gets the steps reported.</summary>
    public uint Steps => U32(4);

    /// <summary>Gets the budget spent reported.</summary>
    public uint Spent => U32(8);

    /// <summary>Gets the number of state fingerprints in the trace.</summary>
    public uint TraceCount => U32(12);

    /// <summary>Gets the submitter id.</summary>
    public uint Submitter => U32(16);

    /// <summary>Gets the state length.</summary>
    public uint StateLength => U32(20);

    /// <summary>Gets the SHA-256 of the state as found.</summary>
    public ReadOnlySpan<byte> StateFoundHash => Slice(24, 32);

    /// <summary>Gets the SHA-256 of the state after rollback.</summary>
    public ReadOnlySpan<byte> StateRestoredHash => Slice(56, 32);

    /// <summary>Gets the signature to share with peers (they add it to their registries as destructive).</summary>
    public ReadOnlySpan<byte> Signature => Slice(88, 32);

    /// <summary>Gets the proof hash, SHA-256("zxv/pdx-repair/v1" || bytes 0..119).</summary>
    public ReadOnlySpan<byte> ProofHash => Slice(120, 32);

    /// <summary>Gets the quarantine record's index.</summary>
    public ulong QuarantineIndex => _bytes is null ? 0 : BinaryPrimitives.ReadUInt64LittleEndian(_bytes.AsSpan(152));

    /// <summary>Wraps proof bytes (not verified: call <see cref="Verify"/>).</summary>
    /// <param name="bytes"><see cref="Length"/> bytes.</param>
    /// <returns>The proof.</returns>
    public static RepairProof FromBytes(ReadOnlySpan<byte> bytes)
    {
        NativeBuffers.RequireLength(bytes, Length, nameof(bytes));
        return new RepairProof(bytes.ToArray());
    }

    /// <summary>Equality.</summary>
    /// <param name="left">The left value.</param>
    /// <param name="right">The right value.</param>
    /// <returns>Whether they are byte for byte equal.</returns>
    public static bool operator ==(RepairProof left, RepairProof right) => left.Equals(right);

    /// <summary>Inequality.</summary>
    /// <param name="left">The left value.</param>
    /// <param name="right">The right value.</param>
    /// <returns>Whether they differ.</returns>
    public static bool operator !=(RepairProof left, RepairProof right) => !left.Equals(right);

    /// <summary>Recomputes the binding hash.</summary>
    /// <returns><see langword="true"/> when it matches; <see langword="false"/> on <see cref="ZxvStatus.VerificationFailed"/>.</returns>
    public unsafe bool Verify()
    {
        if (_bytes is null)
        {
            return false;
        }

        fixed (byte* p = _bytes)
        {
            int rc = NativeMethods.ParadoxRepairVerify(p);
            if (rc == (int)ZxvStatus.VerificationFailed)
            {
                return false;
            }

            NativeCall.Check(rc, "RepairProof.Verify");
            return true;
        }
    }

    /// <summary>A copy of the bytes.</summary>
    /// <returns>The bytes.</returns>
    public byte[] ToArray() => Bytes.ToArray();

    /// <inheritdoc/>
    public bool Equals(RepairProof other) => Bytes.SequenceEqual(other.Bytes);

    /// <inheritdoc/>
    public override bool Equals(object? obj) => obj is RepairProof p && Equals(p);

    /// <inheritdoc/>
    public override int GetHashCode()
    {
        var h = default(HashCode);
        h.AddBytes(Bytes);
        return h.ToHashCode();
    }

    /// <inheritdoc/>
    public override string ToString() => Convert.ToHexString(Bytes).ToLowerInvariant();

    internal static RepairProof Wrap(byte[] bytes) => new(bytes);

    private uint U32(int at) => _bytes is null ? 0 : BinaryPrimitives.ReadUInt32LittleEndian(_bytes.AsSpan(at));

    private ReadOnlySpan<byte> Slice(int at, int n) => _bytes is null ? default : _bytes.AsSpan(at, n);
}

/// <summary>
/// A draft classification of a frontier record, for review
/// (<see cref="Length"/> bytes): the record index, its hash, its signature,
/// and the proposed kind, reason and concerns. A value type over the bytes.
/// </summary>
public readonly struct ParadoxDraft : IEquatable<ParadoxDraft>
{
    /// <summary>The draft length (<c>ZXV_PDX_DRAFT_BYTES</c>).</summary>
    public const int Length = 71;

    private readonly byte[]? _bytes;

    private ParadoxDraft(byte[] bytes)
    {
        _bytes = bytes;
    }

    /// <summary>Gets the bytes (empty for <c>default</c>).</summary>
    public ReadOnlySpan<byte> Bytes => _bytes;

    /// <summary>Gets the frontier record index.</summary>
    public uint Record => _bytes is null ? 0 : BinaryPrimitives.ReadUInt32LittleEndian(_bytes);

    /// <summary>Gets the record's hash.</summary>
    public ReadOnlySpan<byte> RecordHash => _bytes is null ? default : _bytes.AsSpan(4, 32);

    /// <summary>Gets the record's signature.</summary>
    public ReadOnlySpan<byte> Signature => _bytes is null ? default : _bytes.AsSpan(36, 32);

    /// <summary>Gets the proposed kind.</summary>
    public ParadoxKind Kind => _bytes is null ? ParadoxKind.None : (ParadoxKind)_bytes[68];

    /// <summary>Gets the caller's reason code.</summary>
    public byte Reason => _bytes is null ? (byte)0 : _bytes[69];

    /// <summary>Gets the money or device flags.</summary>
    public ParadoxConcerns Concerns => _bytes is null ? ParadoxConcerns.None : (ParadoxConcerns)_bytes[70];

    /// <summary>Wraps draft bytes (for example one a peer sent); <see cref="ParadoxBook.Review"/> checks them.</summary>
    /// <param name="bytes"><see cref="Length"/> bytes.</param>
    /// <returns>The draft.</returns>
    public static ParadoxDraft FromBytes(ReadOnlySpan<byte> bytes)
    {
        NativeBuffers.RequireLength(bytes, Length, nameof(bytes));
        return new ParadoxDraft(bytes.ToArray());
    }

    /// <summary>Equality.</summary>
    /// <param name="left">The left value.</param>
    /// <param name="right">The right value.</param>
    /// <returns>Whether they are byte for byte equal.</returns>
    public static bool operator ==(ParadoxDraft left, ParadoxDraft right) => left.Equals(right);

    /// <summary>Inequality.</summary>
    /// <param name="left">The left value.</param>
    /// <param name="right">The right value.</param>
    /// <returns>Whether they differ.</returns>
    public static bool operator !=(ParadoxDraft left, ParadoxDraft right) => !left.Equals(right);

    /// <summary>A copy of the bytes.</summary>
    /// <returns>The bytes.</returns>
    public byte[] ToArray() => Bytes.ToArray();

    /// <inheritdoc/>
    public bool Equals(ParadoxDraft other) => Bytes.SequenceEqual(other.Bytes);

    /// <inheritdoc/>
    public override bool Equals(object? obj) => obj is ParadoxDraft d && Equals(d);

    /// <inheritdoc/>
    public override int GetHashCode()
    {
        var h = default(HashCode);
        h.AddBytes(Bytes);
        return h.ToHashCode();
    }

    /// <inheritdoc/>
    public override string ToString() => Convert.ToHexString(Bytes).ToLowerInvariant();

    internal static ParadoxDraft Wrap(byte[] bytes) => new(bytes);

    internal byte[] RequireBytes() => _bytes ?? throw new ArgumentException("The draft is empty (default).");
}

/// <summary>
/// The pure parts of the paradox engine (ABI 1.5, <c>zxv_paradox_*</c>):
/// signatures, the tag-to-kind table, actions and names. No context, no capability.
/// </summary>
/// <remarks>
/// HONEST LIMITS. This is contradiction-tolerant bookkeeping: it contains,
/// holds and routes contradictions and never makes both sides true. It does
/// not decide which side is really true. Detection is only by the checks the
/// caller runs. Integers only.
/// </remarks>
public static class ParadoxEngine
{
    /// <summary>The signature of an anomaly: SHA-256("zxv/pdx-sig/v1" || data) of a canonical description the caller builds.</summary>
    /// <param name="description">The canonical description.</param>
    /// <returns>32 bytes.</returns>
    public static unsafe byte[] Signature(ReadOnlySpan<byte> description)
    {
        var sig = new byte[32];
        fixed (byte* d = description)
        fixed (byte* s = sig)
        {
            NativeCall.Check(NativeMethods.ParadoxSignature(d, (nuint)description.Length, s), "ParadoxEngine.Signature");
        }

        return sig;
    }

    /// <summary>The kind a tag reads as on its own (a forbidden tag reads as <see cref="ParadoxKind.None"/>: forbidden is not a kind).</summary>
    /// <param name="tag">The tag.</param>
    /// <returns>The kind.</returns>
    public static ParadoxKind KindOf(Hexagram tag)
    {
        NativeCall.Check(NativeMethods.ParadoxKindOfTag(tag.Tag, out int kind), "ParadoxEngine.KindOf");
        return (ParadoxKind)kind;
    }

    /// <summary>What to do with a classified pair; a forbidden tag gives <see cref="ParadoxAction.Rollback"/> whatever the kind.</summary>
    /// <param name="kind">The kind.</param>
    /// <param name="tag">The tag.</param>
    /// <returns>The action.</returns>
    /// <exception cref="ZxvUsageException">An invalid pair (<see cref="ZxvStatus.InvalidArgument"/>).</exception>
    public static ParadoxAction ActionFor(ParadoxKind kind, Hexagram tag)
    {
        NativeCall.Check(NativeMethods.ParadoxAction((int)kind, tag.Tag, out int action), "ParadoxEngine.ActionFor");
        return (ParadoxAction)action;
    }

    /// <summary>The hexagram action an outcome amounts to (Applied to Proceed, Deferred to Wait, ProposalForOwner to Escalate, Contained to Rollback, ...).</summary>
    /// <param name="outcome">The outcome.</param>
    /// <returns>The action.</returns>
    public static HexAction OutcomeAction(ParadoxOutcome outcome)
    {
        NativeCall.Check(NativeMethods.ParadoxOutcomeAction((int)outcome, out int action), "ParadoxEngine.OutcomeAction");
        return (HexAction)action;
    }

    /// <summary>Classifies a tag without a registry lookup.</summary>
    /// <param name="tag">The tag.</param>
    /// <param name="evidence">Counts before and after, or <see langword="null"/> for no trend.</param>
    /// <param name="stale">The newest evidence is past the caller's freshness limit.</param>
    /// <param name="signature">The anomaly's 32-byte signature, or empty.</param>
    /// <returns>The class pair.</returns>
    public static ParadoxClassification Classify(Hexagram tag, ParadoxEvidence? evidence = null, bool stale = false, ReadOnlySpan<byte> signature = default) =>
        ParadoxBook.ClassifyCore(IntPtr.Zero, tag, evidence, stale, signature, null);

    /// <summary>The name of a kind ("constructive", ...; "INVALID" for an unknown value).</summary>
    /// <param name="kind">The kind.</param>
    /// <returns>The name.</returns>
    public static string KindName(ParadoxKind kind) => Marshal.PtrToStringUTF8(NativeMethods.ParadoxKindName((int)kind)) ?? string.Empty;

    /// <summary>The name of an outcome ("proposal-for-owner", ...).</summary>
    /// <param name="outcome">The outcome.</param>
    /// <returns>The name.</returns>
    public static string OutcomeName(ParadoxOutcome outcome) => Marshal.PtrToStringUTF8(NativeMethods.ParadoxOutcomeName((int)outcome)) ?? string.Empty;

    /// <summary>The name of a node state ("sandboxed", ...).</summary>
    /// <param name="state">The state.</param>
    /// <returns>The name.</returns>
    public static string NodeStateName(ParadoxNodeState state) => Marshal.PtrToStringUTF8(NativeMethods.ParadoxNodeStateName((int)state)) ?? string.Empty;
}
