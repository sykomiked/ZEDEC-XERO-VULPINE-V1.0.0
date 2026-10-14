// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Runtime.CompilerServices;
using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;
using Zxv.Interop;

namespace Zxv.Freight;

/// <summary>The built-in SEED generators (<c>ZXV_FREIGHT_GEN_*</c>).</summary>
public enum FreightGenerator : byte
{
    /// <summary>xorshift64* bytes; params: 8 bytes, a nonzero u64 LE seed.</summary>
    Xorshift = 1,

    /// <summary>A pattern repeated; params: the 1..32-byte pattern.</summary>
    Repeat = 2,

    /// <summary>The Fibonacci word abaababaab...; params: 2 distinct bytes a, b.</summary>
    FibonacciWord = 3,

    /// <summary>A Sierpinski pattern; params: log2 width (1..15), on byte, off byte.</summary>
    Sierpinski = 4,
}

/// <summary>What an op stream declares.</summary>
/// <param name="Total">The bytes it expands to.</param>
/// <param name="HasSha256">Whether it carries the output's SHA-256, which expansion verifies.</param>
public readonly record struct FreightOpsInfo(ulong Total, bool HasSha256);

/// <summary>
/// Resolves a CIDREF op while an op stream expands: write exactly
/// <c>output.Length</c> bytes for <paramref name="cid"/> and return
/// <see langword="true"/>, or return <see langword="false"/> if it cannot.
/// It must not expand another stream from inside. An exception it throws is
/// caught at the native boundary and rethrown from
/// <see cref="FreightOps.Expand"/>.
/// </summary>
/// <param name="cid">The content id bytes.</param>
/// <param name="output">Where to write the bytes.</param>
/// <returns>Whether it resolved.</returns>
public delegate bool CidRefResolver(ReadOnlySpan<byte> cid, Span<byte> output);

/// <summary>
/// Op streams (ABI 1.5): a "ZXOP" header, then LITERAL, COPY (an LZ77 back
/// reference), CIDREF (bytes the receiver resolves by content id) and SEED
/// (a built-in generator) ops that the receiver expands, checked against the
/// caller's limit before a byte is written. Pure: no context, no capability.
/// </summary>
/// <remarks>
/// HONEST LIMITS. No size guarantee: random data encodes slightly LARGER than
/// itself, COPY helps only data that repeats, CIDREF only names bytes the
/// receiver must already hold, and SEED only reproduces the built-in
/// generators (no encoder searches for seeds). No information is created.
/// </remarks>
public static class FreightOps
{
    /// <summary>The largest stream or expansion handled (<c>ZXV_FREIGHT_OPS_MAX</c>, 64 MiB).</summary>
    public const long MaxBytes = 67_108_864;

    /// <summary>The longest CID in a CIDREF (<c>ZXV_FREIGHT_CID_MAX</c>).</summary>
    public const int MaxCidBytes = 128;

    /// <summary>The longest SEED params (<c>ZXV_FREIGHT_GEN_PARAM_MAX</c>).</summary>
    public const int MaxGeneratorParams = 32;

    /// <summary>Reads a stream's declared total and whether it carries a SHA-256, without expanding.</summary>
    /// <param name="ops">The stream.</param>
    /// <returns>What it declares.</returns>
    /// <exception cref="ZxvUsageException">A bad header (<see cref="ZxvStatus.InvalidFormat"/>).</exception>
    public static unsafe FreightOpsInfo Peek(ReadOnlySpan<byte> ops)
    {
        fixed (byte* p = ops)
        {
            NativeCall.Check(NativeMethods.FreightOpsPeek(p, (nuint)ops.Length, out ulong total, out int sha), "FreightOps.Peek");
            return new FreightOpsInfo(total, sha != 0);
        }
    }

    /// <summary>
    /// Expands <paramref name="ops"/>. A declared total above
    /// <paramref name="limit"/> is refused before anything is written.
    /// </summary>
    /// <param name="ops">The stream.</param>
    /// <param name="limit">The most bytes accepted, at most <see cref="MaxBytes"/>.</param>
    /// <param name="resolver">Resolves CIDREF ops, or <see langword="null"/> (then a CIDREF fails with <see cref="ZxvStatus.NotFound"/>).</param>
    /// <returns>The expanded bytes.</returns>
    /// <exception cref="ZxvUsageException">Over the limit (<see cref="ZxvStatus.TooLong"/>), malformed (<see cref="ZxvStatus.InvalidFormat"/>), or a CIDREF not resolved (<see cref="ZxvStatus.NotFound"/>).</exception>
    /// <exception cref="VerificationFailedException">The output fails the stream's SHA-256.</exception>
    public static unsafe byte[] Expand(ReadOnlySpan<byte> ops, long limit, CidRefResolver? resolver = null)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(limit);
        var state = new ResolveState(resolver);
        GCHandle gc = GCHandle.Alloc(state);
        try
        {
            fixed (byte* p = ops)
            {
                byte* o = p;
                nuint n = (nuint)ops.Length;
                void* user = (void*)GCHandle.ToIntPtr(gc);
                bool hasResolver = resolver is not null;
                return NativeBuffers.ReadBytes(
                    (byte* b, nuint c, out nuint l) => NativeMethods.FreightOpsExpand(o, n, (ulong)limit, hasResolver ? &OnResolve : null, user, b, c, out l),
                    rc =>
                    {
                        if (state.Failure is { } failure)
                        {
                            ExceptionDispatchInfo.Capture(failure).Throw();
                        }

                        NativeCall.Check(rc, "FreightOps.Expand");
                    });
            }
        }
        finally
        {
            gc.Free();
            GC.KeepAlive(resolver);
        }
    }

    /// <summary>Greedy LZ77 encoder (deterministic): <paramref name="input"/> as a LITERAL/COPY stream. The result may be LARGER than the input.</summary>
    /// <param name="input">At most <see cref="MaxBytes"/> bytes.</param>
    /// <param name="withSha256">Carry the output's SHA-256 so expansion verifies it.</param>
    /// <returns>The stream.</returns>
    public static unsafe byte[] Encode(ReadOnlySpan<byte> input, bool withSha256 = true)
    {
        fixed (byte* p = input)
        {
            byte* i = p;
            nuint n = (nuint)input.Length;
            int sha = withSha256 ? 1 : 0;
            return NativeBuffers.ReadBytes((byte* b, nuint c, out nuint l) => NativeMethods.FreightOpsEncode(i, n, sha, b, c, out l), rc => NativeCall.Check(rc, "FreightOps.Encode"));
        }
    }

    /// <summary>The worst-case encoded size of <paramref name="length"/> input bytes (all literals).</summary>
    /// <param name="length">The input length.</param>
    /// <param name="withSha256">Whether the stream carries a SHA-256.</param>
    /// <returns>The bound.</returns>
    public static ulong Bound(ulong length, bool withSha256 = true)
    {
        NativeCall.Check(NativeMethods.FreightOpsBound(length, withSha256 ? 1 : 0, out ulong bound), "FreightOps.Bound");
        return bound;
    }

    /// <summary>Runs one SEED generator directly.</summary>
    /// <param name="generator">The generator.</param>
    /// <param name="parameters">Its params (at most 32 bytes).</param>
    /// <param name="length">Bytes to produce.</param>
    /// <returns>The output.</returns>
    /// <exception cref="ZxvUsageException">An unknown generator or params it refuses (<see cref="ZxvStatus.InvalidArgument"/>).</exception>
    public static unsafe byte[] RunGenerator(FreightGenerator generator, ReadOnlySpan<byte> parameters, int length)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(length);
        var output = new byte[length];
        fixed (byte* p = parameters)
        fixed (byte* o = output)
        {
            NativeCall.Check(NativeMethods.FreightGenRun((byte)generator, p, (nuint)parameters.Length, o, (nuint)length), "FreightOps.RunGenerator");
        }

        return output;
    }

#pragma warning disable CA1031 // A resolver's exception must never unwind into native code; it is rethrown after the expansion returns.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static unsafe int OnResolve(void* user, byte* cid, nuint cidLen, byte* output, ulong len)
    {
        ResolveState? state = null;
        try
        {
            state = (ResolveState)GCHandle.FromIntPtr((IntPtr)user).Target!;
            if (state.Resolver is null || len > int.MaxValue)
            {
                return (int)ZxvStatus.NotFound;
            }

            return state.Resolver(new ReadOnlySpan<byte>(cid, checked((int)cidLen)), new Span<byte>(output, (int)len))
                ? (int)ZxvStatus.Ok
                : (int)ZxvStatus.NotFound;
        }
        catch (Exception ex)
        {
            if (state is not null)
            {
                state.Failure ??= ex;
            }

            return (int)ZxvStatus.Internal;
        }
    }
#pragma warning restore CA1031

    private sealed class ResolveState
    {
        internal ResolveState(CidRefResolver? resolver)
        {
            Resolver = resolver;
        }

        internal CidRefResolver? Resolver { get; }

        internal Exception? Failure { get; set; }
    }
}

/// <summary>
/// Builds an op stream op by op (<c>zxv_freight_ops_writer</c>). The first
/// failing put is sticky: every later put and <see cref="Finish"/> fail with
/// it again. Holds only its own memory; calls are serialized by a managed lock.
/// </summary>
public sealed class FreightOpsWriter : IDisposable
{
    private readonly ZxvFreightOpsWriterHandle _handle;
    private readonly object _gate = new();

    /// <summary>Creates a writer.</summary>
    /// <param name="declaredTotal">What the ops must produce in all.</param>
    /// <param name="sha256">The SHA-256 of that output (32 bytes) for expansion to verify, or empty for none.</param>
    /// <param name="maxBytes">The writer's buffer: 14 (46 with a SHA-256)..<see cref="FreightOps.MaxBytes"/>.</param>
    public unsafe FreightOpsWriter(ulong declaredTotal, ReadOnlySpan<byte> sha256, long maxBytes)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(maxBytes);
        if (!sha256.IsEmpty)
        {
            NativeBuffers.RequireLength(sha256, 32, nameof(sha256));
        }

        int rc;
        fixed (byte* s = sha256)
        {
            rc = NativeMethods.FreightOpsWriterCreate(declaredTotal, sha256.IsEmpty ? null : s, (ulong)maxBytes, out _handle);
        }

        if (rc != (int)ZxvStatus.Ok)
        {
            _handle.Dispose();
            NativeCall.Check(rc, "FreightOpsWriter.Create");
        }
    }

    /// <summary>Appends a LITERAL op.</summary>
    /// <param name="bytes">1 or more bytes, not past the declared total.</param>
    /// <exception cref="ZxvException">A bad length (<see cref="ZxvStatus.InvalidArgument"/>), or the buffer is full (<see cref="ZxvStatus.CapacityExceeded"/>).</exception>
    public unsafe void PutLiteral(ReadOnlySpan<byte> bytes)
    {
        lock (_gate)
        {
            Enter();
            fixed (byte* p = bytes)
            {
                NativeCall.Check(NativeMethods.FreightOpsPutLiteral(_handle, p, (nuint)bytes.Length), "FreightOpsWriter.PutLiteral");
            }
        }
    }

    /// <summary>Appends a COPY op: <paramref name="length"/> bytes from <paramref name="distance"/> back (overlap allowed).</summary>
    /// <param name="distance">1..bytes produced so far.</param>
    /// <param name="length">1 or more, not past the declared total.</param>
    public void PutCopy(ulong distance, ulong length)
    {
        lock (_gate)
        {
            Enter();
            NativeCall.Check(NativeMethods.FreightOpsPutCopy(_handle, distance, length), "FreightOpsWriter.PutCopy");
        }
    }

    /// <summary>Appends a CIDREF op: <paramref name="length"/> bytes the receiver resolves by <paramref name="cid"/>.</summary>
    /// <param name="cid">1..128 bytes.</param>
    /// <param name="length">1 or more, not past the declared total.</param>
    public unsafe void PutCidRef(ReadOnlySpan<byte> cid, ulong length)
    {
        lock (_gate)
        {
            Enter();
            fixed (byte* p = cid)
            {
                NativeCall.Check(NativeMethods.FreightOpsPutCidref(_handle, p, (nuint)cid.Length, length), "FreightOpsWriter.PutCidRef");
            }
        }
    }

    /// <summary>Appends a SEED op: <paramref name="length"/> bytes of a built-in generator.</summary>
    /// <param name="generator">The generator.</param>
    /// <param name="parameters">Its params.</param>
    /// <param name="length">1 or more, not past the declared total.</param>
    public unsafe void PutSeed(FreightGenerator generator, ReadOnlySpan<byte> parameters, ulong length)
    {
        lock (_gate)
        {
            Enter();
            fixed (byte* p = parameters)
            {
                NativeCall.Check(NativeMethods.FreightOpsPutSeed(_handle, (byte)generator, p, (nuint)parameters.Length, length), "FreightOpsWriter.PutSeed");
            }
        }
    }

    /// <summary>The finished stream. The writer stays usable (finish again after more puts).</summary>
    /// <returns>The stream bytes.</returns>
    /// <exception cref="ZxvException">The ops produce fewer bytes than declared (<see cref="ZxvStatus.InvalidState"/>), or an earlier put's sticky error.</exception>
    public unsafe byte[] Finish()
    {
        lock (_gate)
        {
            Enter();
            return NativeBuffers.ReadBytes((byte* b, nuint c, out nuint l) => NativeMethods.FreightOpsWriterFinish(_handle, b, c, out l), rc => NativeCall.Check(rc, "FreightOpsWriter.Finish"));
        }
    }

    /// <summary>Releases the writer.</summary>
    public void Dispose()
    {
        lock (_gate)
        {
            _handle.Dispose();
        }
    }

    private void Enter() => ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
}
