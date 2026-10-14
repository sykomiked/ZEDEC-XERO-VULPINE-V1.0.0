// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Diagnostics.CodeAnalysis;
using Zxv.Interop;

namespace Zxv.Aipi;

/// <summary>Multicodec codes a <see cref="Cid"/> can carry (<c>ZXV_CID_CODEC_*</c>, <c>ZXV_AIPI_MC_*</c>).</summary>
/// <remarks>The <c>0x3A10xx</c> AIPI codes are multicodec PRIVATE-USE values; they are not registered.</remarks>
public static class CidCodec
{
    /// <summary>Raw bytes, 0x55: blobs, modules, contracts.</summary>
    public const uint Raw = 0x55;

    /// <summary>dag-pb, 0x70: UnixFS directories and large files.</summary>
    public const uint DagPb = 0x70;

    /// <summary>zxv-fn: a plain AIPI function record (intent 0).</summary>
    public const uint AipiFunction = 0x3A1000;

    /// <summary>zxv-tensor-infer: an inference function record (intent 1).</summary>
    public const uint AipiTensorInfer = 0x3A1001;

    /// <summary>zxv-settle-ledger: a ledger settlement function record (intent 2).</summary>
    public const uint AipiSettleLedger = 0x3A1002;

    /// <summary>zxv-swarm-route: a swarm routing function record (intent 3).</summary>
    public const uint AipiSwarmRoute = 0x3A1003;

    /// <summary>zxv-aipi-call: a call record naming one invocation.</summary>
    public const uint AipiCall = 0x3A10C0;
}

/// <summary>Multihash functions a <see cref="Cid"/> can use (<c>ZXV_CID_HASH_*</c>).</summary>
public static class CidHashFunction
{
    /// <summary>Identity: the "digest" is the bytes themselves (at most 64).</summary>
    public const uint Identity = 0x00;

    /// <summary>SHA2-256.</summary>
    public const uint Sha256 = 0x12;
}

/// <summary>The text form of a <see cref="Cid"/> (<c>ZXV_CID_BASE_*</c>).</summary>
public enum CidBase
{
    /// <summary>What Kubo prints: CIDv1 as "b..." base32, CIDv0 as "Qm..." base58btc.</summary>
    Default = 0,

    /// <summary>"z..." base58btc, the AIPI form (CIDv1 only; a CIDv0 has no multibase form).</summary>
    Base58Btc = 1,
}

/// <summary>The parts of a <see cref="Cid"/>.</summary>
/// <param name="Version">0 or 1.</param>
/// <param name="Codec">The multicodec (<see cref="CidCodec"/>).</param>
/// <param name="HashFunction">The multihash function (<see cref="CidHashFunction"/>).</param>
/// <param name="Digest">The digest bytes (32 for SHA2-256, the content itself for identity).</param>
public sealed record CidInfo(uint Version, uint Codec, uint HashFunction, ReadOnlyMemory<byte> Digest);

/// <summary>
/// A content identifier: a name for bytes made from WHAT they are (a hash),
/// not WHERE they live. A value type over the binary CID (version, codec,
/// multihash; at most <see cref="MaxBytes"/> bytes), compared byte for byte.
/// </summary>
/// <remarks>
/// <para>
/// Every CID here is byte-for-byte the one Kubo makes:
/// <see cref="Compute"/> is <c>ipfs block put</c> (CIDv1, SHA2-256 of the
/// bytes as one block), <see cref="ForFile"/> is <c>ipfs add --cid-version=1</c>
/// (256 KiB chunks, raw leaves, balanced DAG), and <see cref="ToString()"/>
/// prints what Kubo prints. Only canonical CIDv0 and CIDv1 with a SHA2-256 or
/// identity multihash are accepted; anything else is refused with
/// <see cref="ZxvStatus.CidMismatch"/>.
/// </para>
/// <para>
/// HONEST LIMITS. A CID proves which bytes are meant. It does not make them
/// available (it resolves only while someone holds them) and it does not
/// prove that a claimed function output is right.
/// </para>
/// </remarks>
public readonly struct Cid : IEquatable<Cid>
{
    /// <summary>The longest binary CID accepted or produced (<c>ZXV_CID_MAX</c>).</summary>
    public const int MaxBytes = 80;

    /// <summary>The longest CID text (<c>ZXV_CID_TEXT_MAX</c>).</summary>
    public const int MaxTextLength = 139;

    /// <summary>The longest multihash digest (<c>ZXV_CID_DIGEST_MAX</c>, identity).</summary>
    public const int MaxDigestBytes = 64;

    private readonly byte[]? _bytes;

    private Cid(byte[] bytes)
    {
        _bytes = bytes;
    }

    /// <summary>Gets a value indicating whether this is the default (no CID).</summary>
    public bool IsEmpty => _bytes is null;

    /// <summary>Gets the binary CID.</summary>
    public ReadOnlySpan<byte> Bytes => _bytes;

    /// <summary>Gets the binary length in bytes.</summary>
    public int Length => _bytes?.Length ?? 0;

    /// <summary>Gets the CID's parts.</summary>
    /// <exception cref="InvalidOperationException">The CID is empty.</exception>
    public unsafe CidInfo Info
    {
        get
        {
            byte[] b = Require();
            byte* digest = stackalloc byte[MaxDigestBytes];
            fixed (byte* p = b)
            {
                NativeCall.Check(NativeMethods.CidInfo(p, (nuint)b.Length, out uint v, out uint codec, out uint hash, digest, out nuint dl), "Cid.Info");
                return new CidInfo(v, codec, hash, new ReadOnlySpan<byte>(digest, checked((int)dl)).ToArray());
            }
        }
    }

    /// <summary>Equality by bytes.</summary>
    /// <param name="left">The first CID.</param>
    /// <param name="right">The second CID.</param>
    /// <returns><see langword="true"/> when both name the same bytes the same way.</returns>
    public static bool operator ==(Cid left, Cid right) => left.Equals(right);

    /// <summary>Inequality by bytes.</summary>
    /// <param name="left">The first CID.</param>
    /// <param name="right">The second CID.</param>
    /// <returns><see langword="true"/> when they differ.</returns>
    public static bool operator !=(Cid left, Cid right) => !left.Equals(right);

    /// <summary>
    /// The CIDv1 of <paramref name="data"/> as one block: SHA2-256 under
    /// <paramref name="codec"/> (Kubo's <c>ipfs block put</c>).
    /// </summary>
    /// <param name="data">The bytes.</param>
    /// <param name="codec">The multicodec; <see cref="CidCodec.Raw"/> for plain bytes.</param>
    /// <returns>The CID.</returns>
    public static unsafe Cid Compute(ReadOnlySpan<byte> data, uint codec = CidCodec.Raw)
    {
        ZxvLibrary.EnsureCompatible();
        byte* o = stackalloc byte[MaxBytes];
        fixed (byte* d = data)
        {
            NativeCall.Check(NativeMethods.CidCompute(codec, d, (nuint)data.Length, o, out nuint n), "Cid.Compute");
            return Wrap(o, n);
        }
    }

    /// <summary>
    /// The root CID Kubo gives <paramref name="data"/> as a file
    /// (<c>ipfs add --cid-version=1</c>, default settings): a raw CID up to
    /// 262 144 bytes, a dag-pb root above. The blocks themselves are not kept.
    /// </summary>
    /// <param name="data">The file's bytes.</param>
    /// <returns>The root CID.</returns>
    public static unsafe Cid ForFile(ReadOnlySpan<byte> data)
    {
        ZxvLibrary.EnsureCompatible();
        byte* o = stackalloc byte[MaxBytes];
        fixed (byte* d = data)
        {
            NativeCall.Check(NativeMethods.CidUnixfs(d, (nuint)data.Length, o, out nuint n), "Cid.ForFile");
            return Wrap(o, n);
        }
    }

    /// <summary>
    /// Parses canonical CID text: "b..." (CIDv1, lower-case base32),
    /// "Qm..." (CIDv0) or "z..." (CIDv1, base58btc). Upper case, padding and
    /// other multibases are refused.
    /// </summary>
    /// <param name="text">The text.</param>
    /// <returns>The CID.</returns>
    /// <exception cref="ZxvRefusedException">Not a canonical CID (<see cref="ZxvStatus.CidMismatch"/>).</exception>
    /// <exception cref="ZxvUsageException">Empty or longer than <see cref="MaxTextLength"/>.</exception>
    public static unsafe Cid Parse(string text)
    {
        byte[] z = NativeBuffers.Utf8Z(text, nameof(text), allowEmpty: true);
        ZxvLibrary.EnsureCompatible();
        byte* o = stackalloc byte[MaxBytes];
        fixed (byte* t = z)
        {
            NativeCall.Check(NativeMethods.CidFromText(t, o, out nuint n), "Cid.Parse");
            return Wrap(o, n);
        }
    }

    /// <summary>Parses CID text without throwing for text that is not a canonical CID.</summary>
    /// <param name="text">The text.</param>
    /// <param name="cid">The CID, when parsed.</param>
    /// <returns><see langword="true"/> when <paramref name="text"/> is a canonical CID.</returns>
    public static unsafe bool TryParse(string? text, out Cid cid)
    {
        cid = default;
        if (string.IsNullOrEmpty(text) || text.Contains('\0', StringComparison.Ordinal))
        {
            return false;
        }

        byte[] z = NativeBuffers.Utf8Z(text, nameof(text));
        ZxvLibrary.EnsureCompatible();
        byte* o = stackalloc byte[MaxBytes];
        fixed (byte* t = z)
        {
            if (NativeMethods.CidFromText(t, o, out nuint n) != (int)ZxvStatus.Ok)
            {
                return false;
            }

            cid = Wrap(o, n);
            return true;
        }
    }

    /// <summary>Wraps binary CID bytes after checking that they are exactly one canonical CID.</summary>
    /// <param name="bytes">The binary CID.</param>
    /// <returns>The CID (a copy of the bytes).</returns>
    /// <exception cref="ZxvRefusedException">Not a canonical CID (<see cref="ZxvStatus.CidMismatch"/>).</exception>
    public static unsafe Cid FromBytes(ReadOnlySpan<byte> bytes)
    {
        ZxvLibrary.EnsureCompatible();
        byte* digest = stackalloc byte[MaxDigestBytes];
        fixed (byte* p = bytes)
        {
            NativeCall.Check(NativeMethods.CidInfo(p, (nuint)bytes.Length, out _, out _, out _, digest, out _), "Cid.FromBytes");
        }

        return new Cid(bytes.ToArray());
    }

    /// <summary>
    /// Returns whether <paramref name="data"/> is exactly the block this CID
    /// names (re-hashed; identity CIDs compare the bytes). A dag-pb UnixFS root
    /// names its root block, not the file: check a file with <see cref="ForFile"/>.
    /// </summary>
    /// <param name="data">The bytes.</param>
    /// <returns><see langword="true"/> when they match.</returns>
    public unsafe bool Matches(ReadOnlySpan<byte> data)
    {
        byte[] b = Require();
        fixed (byte* c = b)
        fixed (byte* d = data)
        {
            int rc = NativeMethods.CidVerify(c, (nuint)b.Length, d, (nuint)data.Length);
            if (rc == (int)ZxvStatus.CidMismatch)
            {
                return false;
            }

            NativeCall.Check(rc, "Cid.Matches");
            return true;
        }
    }

    /// <summary>The same content as a CIDv1 (a CIDv0 becomes dag-pb CIDv1 with the same hash; Kubo's <c>ipfs cid base32</c>). A CIDv1 is returned unchanged.</summary>
    /// <returns>The CIDv1.</returns>
    public unsafe Cid ToV1()
    {
        byte[] b = Require();
        byte* o = stackalloc byte[MaxBytes];
        fixed (byte* p = b)
        {
            NativeCall.Check(NativeMethods.CidToV1(p, (nuint)b.Length, o, out nuint n), "Cid.ToV1");
            return Wrap(o, n);
        }
    }

    /// <summary>The 32-byte Kademlia key under which providers of this CID are announced on Vinea: SHA-256("zxv/aipi/v1/provider" || binary CID).</summary>
    /// <returns>32 bytes.</returns>
    public unsafe byte[] ProviderKey()
    {
        byte[] b = Require();
        var key = new byte[32];
        fixed (byte* p = b)
        fixed (byte* k = key)
        {
            NativeCall.Check(NativeMethods.AipiProviderKey(p, (nuint)b.Length, k), "Cid.ProviderKey");
        }

        return key;
    }

    /// <summary>The text Kubo prints: "b..." for CIDv1, "Qm..." for CIDv0. The default value prints as "".</summary>
    /// <returns>The text.</returns>
    public override string ToString() => _bytes is null ? string.Empty : ToString(CidBase.Default);

    /// <summary>The CID as text in <paramref name="base"/>.</summary>
    /// <param name="base">The base.</param>
    /// <returns>The text.</returns>
    /// <exception cref="ZxvRefusedException">A CIDv0 in <see cref="CidBase.Base58Btc"/> (<see cref="ZxvStatus.CidMismatch"/>).</exception>
    public unsafe string ToString(CidBase @base)
    {
        byte[] b = Require();
        fixed (byte* p = b)
        {
            byte* pp = p;
            nuint len = (nuint)b.Length;
            return NativeBuffers.ReadText((byte* buf, nuint cap, out nuint l) => NativeMethods.CidToText(pp, len, (uint)@base, buf, cap, out l), rc => NativeCall.Check(rc, "Cid.ToString"));
        }
    }

    /// <summary>A copy of the binary CID.</summary>
    /// <returns>The bytes.</returns>
    public byte[] ToArray() => _bytes is null ? Array.Empty<byte>() : (byte[])_bytes.Clone();

    /// <inheritdoc/>
    public bool Equals(Cid other) => Bytes.SequenceEqual(other.Bytes) && IsEmpty == other.IsEmpty;

    /// <inheritdoc/>
    public override bool Equals([NotNullWhen(true)] object? obj) => obj is Cid c && Equals(c);

    /// <inheritdoc/>
    public override int GetHashCode()
    {
        var h = new HashCode();
        h.AddBytes(Bytes);
        return h.ToHashCode();
    }

    /// <summary>Wraps bytes the native library just produced (already canonical).</summary>
    internal static unsafe Cid Wrap(byte* bytes, nuint length) => new(new ReadOnlySpan<byte>(bytes, checked((int)length)).ToArray());

    private byte[] Require() => _bytes ?? throw new InvalidOperationException("The CID is empty (default).");
}
