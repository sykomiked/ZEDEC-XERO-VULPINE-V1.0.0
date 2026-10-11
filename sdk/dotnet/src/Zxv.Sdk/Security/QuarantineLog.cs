// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using Zxv.Interop;

namespace Zxv;

/// <summary>One record of a <see cref="QuarantineLog"/>.</summary>
/// <param name="Seq">Its index.</param>
/// <param name="Tag">The forbidden tag.</param>
/// <param name="Reason">The caller's reason code.</param>
/// <param name="Submitter">Who submitted the work.</param>
/// <param name="Subject">The 32-byte digest of the refused work.</param>
/// <param name="Prev">The previous record's hash (zeros for the first).</param>
/// <param name="Hash">This record's hash.</param>
public sealed record QuarantineRecord(ulong Seq, Hexagram Tag, byte Reason, uint Submitter, ReadOnlyMemory<byte> Subject, ReadOnlyMemory<byte> Prev, ReadOnlyMemory<byte> Hash);

/// <summary>
/// The quarantine log of ABI 1.3 (<c>zxv_hexlog</c>): an append-only, SHA-256
/// hash-chained record of forbidden work, read for audit and never replayed.
/// Create one with <see cref="ZxvPlatform.CreateQuarantineLog(Capability, int)"/>;
/// creating and appending need <see cref="CapabilityRights.Attest"/>. Thread-safe.
/// </summary>
/// <remarks>
/// Record k's hash is SHA-256("zxv/lpx-qlog/v1" || seq (u64 LE) || tag ||
/// reason || submitter (u32 LE) || subject || prev). A full log refuses the
/// append (<see cref="ZxvStatus.CapacityExceeded"/>); the caller must then halt
/// the platform (<see cref="ZxvPlatform.Halt(string)"/>), because forbidden work
/// it cannot record must not continue. Signing <see cref="GetHead"/> is the caller's job.
/// </remarks>
public sealed class QuarantineLog : IDisposable
{
    /// <summary>The largest capacity (<c>ZXV_HEXLOG_MAX_RECORDS</c>).</summary>
    public const int MaxRecords = 65536;

    private readonly Capability _cap;
    private readonly ZxvQuarantineLogHandle _handle;

    internal QuarantineLog(Capability capability, int capacity)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(capacity);
        _cap = capability;
        capability.Enter("QuarantineLog.Create");
        int rc = NativeMethods.HexlogCreate(capability.Handle, (uint)capacity, out _handle);
        if (rc != (int)ZxvStatus.Ok)
        {
            _handle.Dispose();
            capability.Check(rc, "QuarantineLog.Create");
        }

        _handle.AddDependency(capability.Context.Handle);
    }

    /// <summary>Gets the number of records.</summary>
    public int Count
    {
        get
        {
            Enter("QuarantineLog.Count");
            _cap.Check(NativeMethods.HexlogCount(_handle, out uint n), "QuarantineLog.Count");
            return checked((int)n);
        }
    }

    /// <summary>Quarantines forbidden work.</summary>
    /// <param name="tag">A forbidden tag; any other is refused (<see cref="ZxvUsageException"/>).</param>
    /// <param name="reason">The caller's reason code.</param>
    /// <param name="submitter">Who submitted the work.</param>
    /// <param name="subject">A 32-byte digest of the work.</param>
    /// <returns>The record's sequence number.</returns>
    public unsafe ulong Append(Hexagram tag, byte reason, uint submitter, ReadOnlySpan<byte> subject)
    {
        if (subject.Length != 32)
        {
            throw new ZxvUsageException(ZxvStatus.InvalidArgument, "QuarantineLog.Append", "subject must be 32 bytes");
        }

        Enter("QuarantineLog.Append");
        fixed (byte* s = subject)
        {
            _cap.Check(NativeMethods.HexlogAppend(_cap.Handle, _handle, tag.Tag, reason, submitter, s, out ulong seq), "QuarantineLog.Append");
            return seq;
        }
    }

    /// <summary>The chain head: the last record's hash (32 zero bytes when empty).</summary>
    /// <returns>32 bytes.</returns>
    public unsafe byte[] GetHead()
    {
        Enter("QuarantineLog.GetHead");
        byte[] head = new byte[32];
        fixed (byte* h = head)
        {
            _cap.Check(NativeMethods.HexlogHead(_handle, h), "QuarantineLog.GetHead");
        }

        return head;
    }

    /// <summary>Reads a record, oldest first.</summary>
    /// <param name="index">The record index.</param>
    /// <returns>The record.</returns>
    public unsafe QuarantineRecord GetRecord(int index)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        Enter("QuarantineLog.GetRecord");
        byte[] subject = new byte[32], prev = new byte[32], hash = new byte[32];
        fixed (byte* s = subject, p = prev, h = hash)
        {
            _cap.Check(NativeMethods.HexlogAt(_handle, (uint)index, out ulong seq, out byte tag, out byte reason, out uint who, s, p, h), "QuarantineLog.GetRecord");
            return new QuarantineRecord(seq, Hexagram.FromTag(tag), reason, who, subject, prev, hash);
        }
    }

    /// <summary>Recomputes every link.</summary>
    /// <exception cref="ZxvFatalException">The chain is broken; the platform halts.</exception>
    public void Verify()
    {
        Enter("QuarantineLog.Verify");
        _cap.Check(NativeMethods.HexlogVerify(_handle), "QuarantineLog.Verify");
    }

    /// <summary>Releases the log.</summary>
    public void Dispose() => _handle.Dispose();

    private void Enter(string operation)
    {
        ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
        _cap.Enter(operation);
    }
}
