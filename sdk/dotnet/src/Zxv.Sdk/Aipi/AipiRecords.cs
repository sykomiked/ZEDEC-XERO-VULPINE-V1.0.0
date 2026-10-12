// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using Zxv.Interop;

namespace Zxv.Aipi;

/// <summary>What an AIPI function is for (<c>ZXV_AIPI_INTENT_*</c>); it picks the function CID's codec.</summary>
public enum AipiIntent
{
    /// <summary>A plain function (codec zxv-fn).</summary>
    Function = 0,

    /// <summary>Model inference (codec zxv-tensor-infer).</summary>
    TensorInfer = 1,

    /// <summary>Ledger settlement (codec zxv-settle-ledger).</summary>
    SettleLedger = 2,

    /// <summary>Swarm routing (codec zxv-swarm-route).</summary>
    SwarmRoute = 3,
}

/// <summary>How a result of an AIPI function is checked (<c>ZXV_AIPI_VERIFY_*</c>).</summary>
public enum AipiVerification
{
    /// <summary>A checker export judges (input, output).</summary>
    Checker = 1,

    /// <summary>As <see cref="Checker"/>, with a certificate from a certifier export.</summary>
    Certificate = 2,

    /// <summary>k of n executors agree byte for byte (deterministic functions only).</summary>
    Quorum = 3,

    /// <summary>Never accept a remote result: run it here.</summary>
    Local = 4,
}

/// <summary>Whether a function or block may leave this node (<c>ZXV_AIPI_PRIV_*</c>).</summary>
public enum AipiPrivacy
{
    /// <summary>May be shared and announced.</summary>
    Public = 0,

    /// <summary>Private or key-bearing: sealed at rest and never leaves the node.</summary>
    Local = 1,
}

/// <summary>The route a call would take (<c>ZXV_AIPI_ROUTE_*</c>). This offline library reports only <see cref="Local"/> or <see cref="Unavailable"/>; the others are the kernel's routes for a networked host.</summary>
public enum AipiRoute
{
    /// <summary>A cached result (networked hosts only).</summary>
    Cache = 1,

    /// <summary>The module (and contract) are held here.</summary>
    Local = 2,

    /// <summary>Fetch the code, then run locally (networked hosts only).</summary>
    FetchLocal = 3,

    /// <summary>Send to the mesh (networked hosts only).</summary>
    Mesh = 4,

    /// <summary>The code is not held here, and there is no network.</summary>
    Unavailable = 5,
}

/// <summary>
/// An AIPI function record (<c>zxv_aipi_fn_*</c>): the canonical wire bytes
/// that describe one content-addressed function, decoded strictly. One
/// function has exactly one record and so exactly one <see cref="FunctionCid"/>.
/// </summary>
/// <remarks>Pure: no platform, no capability. Running a function needs the Z-Wasm sandbox, which is not part of libzxv.</remarks>
public sealed class AipiFunction
{
    /// <summary>The longest function record in bytes (<c>ZXV_AIPI_FN_MAX</c>).</summary>
    public const int MaxRecordBytes = 373;

    /// <summary>The longest export name (<c>ZXV_AIPI_NAME_MAX</c>): <c>[A-Za-z0-9_]{1,63}</c>.</summary>
    public const int MaxNameLength = 63;

    /// <summary>The largest quorum n (<c>ZXV_AIPI_QUORUM_MAX</c>).</summary>
    public const int MaxQuorum = 8;

    private readonly byte[] _record;

    private AipiFunction(byte[] record)
    {
        _record = record;
        unsafe
        {
            fixed (byte* r = record)
            {
                nuint n = (nuint)record.Length;
                NativeCall.Check(NativeMethods.AipiFnDecode(r, n, out uint intent, out uint verify, out uint privacy, out uint det, out uint qn, out uint qk, out ulong fuel), "AipiFunction.Decode");
                Intent = (AipiIntent)intent;
                Verification = (AipiVerification)verify;
                Privacy = (AipiPrivacy)privacy;
                IsDeterministic = det != 0;
                QuorumN = (int)qn;
                QuorumK = (int)qk;
                FuelMax = fuel;
                Module = FieldCid(r, n, 0)!.Value;
                Contract = FieldCid(r, n, 1);
                byte* rr = r;
                Entry = NativeBuffers.ReadText((byte* b, nuint c, out nuint l) => NativeMethods.AipiFnGetName(rr, n, 0, b, c, out l), rc => NativeCall.Check(rc, "AipiFunction.Decode"));
                Checker = NativeBuffers.ReadText((byte* b, nuint c, out nuint l) => NativeMethods.AipiFnGetName(rr, n, 1, b, c, out l), rc => NativeCall.Check(rc, "AipiFunction.Decode"));
                Certifier = NativeBuffers.ReadText((byte* b, nuint c, out nuint l) => NativeMethods.AipiFnGetName(rr, n, 2, b, c, out l), rc => NativeCall.Check(rc, "AipiFunction.Decode"));
                byte* o = stackalloc byte[Cid.MaxBytes];
                NativeCall.Check(NativeMethods.AipiFnCid(r, n, o, out nuint cl), "AipiFunction.Decode");
                FunctionCid = Cid.Wrap(o, cl);
            }
        }
    }

    /// <summary>Gets what the function is for.</summary>
    public AipiIntent Intent { get; }

    /// <summary>Gets how its results are checked.</summary>
    public AipiVerification Verification { get; }

    /// <summary>Gets whether it may leave the node.</summary>
    public AipiPrivacy Privacy { get; }

    /// <summary>Gets a value indicating whether the same input always gives the same output.</summary>
    public bool IsDeterministic { get; }

    /// <summary>Gets n of a QUORUM function (0 otherwise).</summary>
    public int QuorumN { get; }

    /// <summary>Gets k of a QUORUM function (0 otherwise).</summary>
    public int QuorumK { get; }

    /// <summary>Gets the most fuel one run may use.</summary>
    public ulong FuelMax { get; }

    /// <summary>Gets the CID of the Z-Wasm module (CIDv1, raw, SHA2-256).</summary>
    public Cid Module { get; }

    /// <summary>Gets the CID of the z-contract text, or <see langword="null"/> for none.</summary>
    public Cid? Contract { get; }

    /// <summary>Gets the export computing the result.</summary>
    public string Entry { get; }

    /// <summary>Gets the checker export ("" unless CHECKER or CERT).</summary>
    public string Checker { get; }

    /// <summary>Gets the certifier export ("" unless CERT).</summary>
    public string Certifier { get; }

    /// <summary>Gets the function CID: codec zxv-fn + intent, SHA2-256 of <see cref="Record"/>.</summary>
    public Cid FunctionCid { get; }

    /// <summary>Gets the canonical record bytes.</summary>
    public ReadOnlySpan<byte> Record => _record;

    /// <summary>
    /// Encodes a canonical function record. Every field is checked; none is masked.
    /// </summary>
    /// <param name="intent">What it is for.</param>
    /// <param name="verification">How results are checked.</param>
    /// <param name="privacy">Whether it may leave the node.</param>
    /// <param name="deterministic">Whether the same input gives the same output (required for QUORUM).</param>
    /// <param name="fuelMax">The most fuel one run may use (above 0).</param>
    /// <param name="module">CIDv1 raw SHA2-256 of the Z-Wasm module bytes.</param>
    /// <param name="entry">The export computing the result: <c>[A-Za-z0-9_]{1,63}</c>.</param>
    /// <param name="checker">CHECKER and CERT only: the checker export (differs from <paramref name="entry"/>).</param>
    /// <param name="certifier">CERT only: the certifier export (differs from the other two).</param>
    /// <param name="contract">CIDv1 raw SHA2-256 of the z-contract text, or <see langword="null"/>.</param>
    /// <param name="quorumN">QUORUM only: 2..8.</param>
    /// <param name="quorumK">QUORUM only: n/2 &lt; k &lt;= n.</param>
    /// <returns>The decoded function, holding its canonical record and CID.</returns>
    /// <exception cref="ZxvUsageException">A field is out of range or breaks a rule (<see cref="ZxvStatus.InvalidArgument"/>), or a name is too long.</exception>
    /// <exception cref="ZxvRefusedException">A CID is not CIDv1 raw SHA2-256 (<see cref="ZxvStatus.CidMismatch"/>).</exception>
    public static unsafe AipiFunction Encode(
        AipiIntent intent,
        AipiVerification verification,
        AipiPrivacy privacy,
        bool deterministic,
        ulong fuelMax,
        Cid module,
        string entry,
        string? checker = null,
        string? certifier = null,
        Cid? contract = null,
        int quorumN = 0,
        int quorumK = 0)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(quorumN);
        ArgumentOutOfRangeException.ThrowIfNegative(quorumK);
        if (module.IsEmpty)
        {
            throw new ArgumentException("A module CID is required.", nameof(module));
        }

        byte[] e = NativeBuffers.Utf8Z(entry, nameof(entry), allowEmpty: true);
        byte[]? ch = checker is null ? null : NativeBuffers.Utf8Z(checker, nameof(checker), allowEmpty: true);
        byte[]? ce = certifier is null ? null : NativeBuffers.Utf8Z(certifier, nameof(certifier), allowEmpty: true);
        byte[] m = module.ToArray();
        byte[] c = contract?.ToArray() ?? Array.Empty<byte>();
        ZxvLibrary.EnsureCompatible();
        fixed (byte* pm = m, pc = c, pe = e, pch = ch, pce = ce)
        {
            byte* xm = pm, xc = c.Length == 0 ? null : pc, xe = pe, xch = pch, xce = pce;
            byte[] rec = NativeBuffers.ReadBytes(
                (byte* b, nuint cap, out nuint l) => NativeMethods.AipiFnEncode((uint)intent, (uint)verification, (uint)privacy, deterministic ? 1u : 0u, (uint)quorumN, (uint)quorumK, fuelMax, xm, (nuint)m.Length, xc, (nuint)c.Length, xe, xch, xce, b, cap, out l),
                rc => NativeCall.Check(rc, "AipiFunction.Encode"));
            return new AipiFunction(rec);
        }
    }

    /// <summary>Checks a function record strictly (exact length, canonical, no trailing bytes) and decodes it.</summary>
    /// <param name="record">The record bytes.</param>
    /// <returns>The function.</returns>
    /// <exception cref="ZxvUsageException">Not a valid, canonical function record (<see cref="ZxvStatus.InvalidFormat"/>).</exception>
    public static AipiFunction Decode(ReadOnlySpan<byte> record)
    {
        ZxvLibrary.EnsureCompatible();
        return new AipiFunction(record.ToArray());
    }

    /// <summary>A copy of the record bytes.</summary>
    /// <returns>The bytes.</returns>
    public byte[] ToArray() => (byte[])_record.Clone();

    private static unsafe Cid? FieldCid(byte* rec, nuint len, uint field)
    {
        byte* o = stackalloc byte[Cid.MaxBytes];
        int rc = NativeMethods.AipiFnGetCid(rec, len, field, o, out nuint n);
        if (rc == (int)ZxvStatus.NotFound)
        {
            return null;
        }

        NativeCall.Check(rc, "AipiFunction.Decode");
        return Cid.Wrap(o, n);
    }
}

/// <summary>The parts of a call record.</summary>
/// <param name="Function">The function CID.</param>
/// <param name="Input">The input bytes.</param>
public sealed record AipiCallParts(Cid Function, ReadOnlyMemory<byte> Input);

/// <summary>
/// AIPI call records (<c>zxv_aipi_call_*</c>): the bytes naming one
/// invocation, a function CID plus its input, and the call CID made from them.
/// Pure: no platform, no capability.
/// </summary>
public static class AipiCall
{
    /// <summary>The largest call input (<c>ZXV_AIPI_IO_MAX</c>).</summary>
    public const int MaxInputBytes = 1024;

    /// <summary>The longest call record (<c>ZXV_AIPI_CALL_MAX</c>).</summary>
    public const int MaxRecordBytes = 1112;

    /// <summary>Encodes the call record of (<paramref name="function"/>, <paramref name="input"/>).</summary>
    /// <param name="function">A function CID (CIDv1 SHA2-256 with a function codec).</param>
    /// <param name="input">At most <see cref="MaxInputBytes"/> bytes.</param>
    /// <returns>The record.</returns>
    /// <exception cref="ZxvRefusedException">Not a function CID (<see cref="ZxvStatus.CidMismatch"/>).</exception>
    public static unsafe byte[] Encode(Cid function, ReadOnlySpan<byte> input)
    {
        byte[] f = function.ToArray();
        ZxvLibrary.EnsureCompatible();
        fixed (byte* pf = f)
        fixed (byte* pi = input)
        {
            byte* xf = pf, xi = pi;
            int il = input.Length;
            return NativeBuffers.ReadBytes((byte* b, nuint c, out nuint l) => NativeMethods.AipiCallEncode(xf, (nuint)f.Length, xi, (nuint)il, b, c, out l), rc => NativeCall.Check(rc, "AipiCall.Encode"));
        }
    }

    /// <summary>The call CID (codec zxv-aipi-call, SHA2-256 of the call record) of (<paramref name="function"/>, <paramref name="input"/>).</summary>
    /// <param name="function">A function CID.</param>
    /// <param name="input">At most <see cref="MaxInputBytes"/> bytes.</param>
    /// <returns>The call CID.</returns>
    public static unsafe Cid ComputeCid(Cid function, ReadOnlySpan<byte> input)
    {
        byte[] f = function.ToArray();
        ZxvLibrary.EnsureCompatible();
        byte* o = stackalloc byte[Cid.MaxBytes];
        fixed (byte* pf = f)
        fixed (byte* pi = input)
        {
            NativeCall.Check(NativeMethods.AipiCallCid(pf, (nuint)f.Length, pi, (nuint)input.Length, o, out nuint n), "AipiCall.ComputeCid");
            return Cid.Wrap(o, n);
        }
    }

    /// <summary>Checks a call record strictly and splits it.</summary>
    /// <param name="record">The record.</param>
    /// <returns>The function CID and input.</returns>
    /// <exception cref="ZxvUsageException">Not a valid call record (<see cref="ZxvStatus.InvalidFormat"/>).</exception>
    public static unsafe AipiCallParts Decode(ReadOnlySpan<byte> record)
    {
        ZxvLibrary.EnsureCompatible();
        fixed (byte* r = record)
        {
            NativeCall.Check(NativeMethods.AipiCallDecode(r, (nuint)record.Length, out nuint fo, out nuint fl, out nuint io, out nuint il), "AipiCall.Decode");
            return new AipiCallParts(Cid.FromBytes(record.Slice((int)fo, (int)fl)), record.Slice((int)io, (int)il).ToArray());
        }
    }
}

/// <summary>The parts of a claim record.</summary>
/// <param name="Call">The call CID answered.</param>
/// <param name="Executor">The executor's 32-byte node id.</param>
/// <param name="Output">The claimed output.</param>
/// <param name="Certificate">The certificate (empty unless the function is CERT).</param>
public sealed record AipiClaimParts(Cid Call, ReadOnlyMemory<byte> Executor, ReadOnlyMemory<byte> Output, ReadOnlyMemory<byte> Certificate);

/// <summary>
/// AIPI claim records (<c>zxv_aipi_claim_*</c>): the bytes an executor signs
/// to answer a call. Signing and checking the signature are the caller's job
/// (for example with <see cref="MlDsa87"/>). A claim is not a CID, and it
/// does not prove that the output is right.
/// </summary>
public static class AipiClaim
{
    /// <summary>The largest claim output (<c>ZXV_AIPI_IO_MAX</c>).</summary>
    public const int MaxOutputBytes = 1024;

    /// <summary>The largest certificate (<c>ZXV_AIPI_CERT_MAX</c>).</summary>
    public const int MaxCertificateBytes = 1024;

    /// <summary>The longest claim record (<c>ZXV_AIPI_CLAIM_MAX</c>).</summary>
    public const int MaxRecordBytes = 2170;

    /// <summary>Encodes a claim: <paramref name="executor"/>'s answer to <paramref name="call"/>.</summary>
    /// <param name="call">A call CID (CIDv1 SHA2-256, codec zxv-aipi-call).</param>
    /// <param name="executor">The executor's 32-byte node id.</param>
    /// <param name="output">At most <see cref="MaxOutputBytes"/> bytes.</param>
    /// <param name="certificate">At most <see cref="MaxCertificateBytes"/> bytes; empty unless CERT.</param>
    /// <returns>The record to sign.</returns>
    public static unsafe byte[] Encode(Cid call, ReadOnlySpan<byte> executor, ReadOnlySpan<byte> output, ReadOnlySpan<byte> certificate = default)
    {
        NativeBuffers.RequireLength(executor, 32, nameof(executor));
        byte[] cc = call.ToArray();
        ZxvLibrary.EnsureCompatible();
        fixed (byte* pc = cc)
        fixed (byte* pe = executor)
        fixed (byte* po = output)
        fixed (byte* pr = certificate)
        {
            byte* xc = pc, xe = pe, xo = po, xr = pr;
            int ol = output.Length, rl = certificate.Length;
            return NativeBuffers.ReadBytes((byte* b, nuint c, out nuint l) => NativeMethods.AipiClaimEncode(xc, (nuint)cc.Length, xe, xo, (nuint)ol, xr, (nuint)rl, b, c, out l), rc => NativeCall.Check(rc, "AipiClaim.Encode"));
        }
    }

    /// <summary>Checks a claim record strictly and splits it.</summary>
    /// <param name="record">The record.</param>
    /// <returns>The parts.</returns>
    /// <exception cref="ZxvUsageException">Not a valid claim record (<see cref="ZxvStatus.InvalidFormat"/>).</exception>
    public static unsafe AipiClaimParts Decode(ReadOnlySpan<byte> record)
    {
        ZxvLibrary.EnsureCompatible();
        fixed (byte* r = record)
        {
            NativeCall.Check(NativeMethods.AipiClaimDecode(r, (nuint)record.Length, out nuint co, out nuint cl, out nuint eo, out nuint oo, out nuint ol, out nuint ro, out nuint rl), "AipiClaim.Decode");
            return new AipiClaimParts(
                Cid.FromBytes(record.Slice((int)co, (int)cl)),
                record.Slice((int)eo, 32).ToArray(),
                record.Slice((int)oo, (int)ol).ToArray(),
                record.Slice((int)ro, (int)rl).ToArray());
        }
    }
}
