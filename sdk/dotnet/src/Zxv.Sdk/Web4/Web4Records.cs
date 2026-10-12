// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using Zxv.Interop;

namespace Zxv.Web4;

/// <summary>Agent cards (ABI 1.5, <c>zxv_w4_card_verify</c>). Pure.</summary>
public static class AgentCard
{
    /// <summary>Verifies a card: well formed, and its binding signature valid under the node key (or the agent's own key when self-bound).</summary>
    /// <param name="card">The card bytes.</param>
    /// <returns>What it says.</returns>
    /// <exception cref="ZxvUsageException">Malformed (<see cref="ZxvStatus.InvalidFormat"/>).</exception>
    /// <exception cref="VerificationFailedException">The signature fails.</exception>
    public static unsafe AgentCardInfo Verify(ReadOnlySpan<byte> card)
    {
        var id = new byte[Web4Agent.IdBytes];
        var peer = new byte[Web4Agent.IdBytes];
        byte* name = stackalloc byte[32];
        fixed (byte* c = card)
        fixed (byte* a = id)
        fixed (byte* p = peer)
        {
            NativeCall.Check(NativeMethods.W4CardVerify(c, (nuint)card.Length, a, p, name, out ulong bound), "AgentCard.Verify");
            return new AgentCardInfo(id, peer, NativeBuffers.FromZ(new ReadOnlySpan<byte>(name, 32)), bound);
        }
    }
}

/// <summary>
/// A directory of verified agent cards, keyed by agent id
/// (<c>zxv_w4_dir</c>). Holds only its own memory; calls are serialized by a
/// managed lock.
/// </summary>
public sealed class AgentDirectory : IDisposable
{
    /// <summary>Most entries (<c>ZXV_W4_DIR_MAX</c>).</summary>
    public const int MaxCapacity = 4096;

    private readonly ZxvW4DirHandle _handle;
    private readonly object _gate = new();

    /// <summary>Creates an empty directory.</summary>
    /// <param name="capacity">1..<see cref="MaxCapacity"/>.</param>
    public AgentDirectory(int capacity)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(capacity);
        int rc = NativeMethods.W4DirCreate((uint)capacity, out _handle);
        if (rc != (int)ZxvStatus.Ok)
        {
            _handle.Dispose();
            NativeCall.Check(rc, "AgentDirectory.Create");
        }
    }

    /// <summary>Gets the number of cards held.</summary>
    public int Count
    {
        get
        {
            lock (_gate)
            {
                Enter();
                NativeCall.Check(NativeMethods.W4DirCount(_handle, out uint n), "AgentDirectory.Count");
                return (int)n;
            }
        }
    }

    /// <summary>Verifies a card, then adds it or replaces the entry for the same agent.</summary>
    /// <param name="card">The card.</param>
    /// <exception cref="VerificationFailedException">The signature fails.</exception>
    /// <exception cref="ZxvException">Malformed, an older binding than the one held (<see cref="ZxvStatus.InvalidState"/>: no rollback), or full.</exception>
    public unsafe void Add(ReadOnlySpan<byte> card)
    {
        lock (_gate)
        {
            Enter();
            fixed (byte* c = card)
            {
                NativeCall.Check(NativeMethods.W4DirAdd(_handle, c, (nuint)card.Length), "AgentDirectory.Add");
            }
        }
    }

    /// <summary>The card held for <paramref name="agentId"/>.</summary>
    /// <param name="agentId">32 bytes.</param>
    /// <returns>The card.</returns>
    /// <exception cref="ZxvUsageException">None held (<see cref="ZxvStatus.NotFound"/>).</exception>
    public unsafe byte[] Find(ReadOnlySpan<byte> agentId)
    {
        NativeBuffers.RequireLength(agentId, Web4Agent.IdBytes, nameof(agentId));
        byte[] id = agentId.ToArray();
        lock (_gate)
        {
            Enter();
            fixed (byte* a = id)
            {
                byte* x = a;
                return NativeBuffers.ReadBytes((byte* b, nuint c, out nuint l) => NativeMethods.W4DirFind(_handle, x, b, c, out l), rc => NativeCall.Check(rc, "AgentDirectory.Find"));
            }
        }
    }

    /// <summary>Like <see cref="Find"/>, but returns <see langword="false"/> when none is held.</summary>
    /// <param name="agentId">32 bytes.</param>
    /// <param name="card">The card when found.</param>
    /// <returns>Whether one is held.</returns>
    public bool TryFind(ReadOnlySpan<byte> agentId, out byte[] card)
    {
        try
        {
            card = Find(agentId);
            return true;
        }
        catch (ZxvUsageException e) when (e.Status == ZxvStatus.NotFound)
        {
            card = Array.Empty<byte>();
            return false;
        }
    }

    /// <summary>Releases the directory.</summary>
    public void Dispose()
    {
        lock (_gate)
        {
            _handle.Dispose();
        }
    }

    private void Enter() => ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
}

/// <summary>
/// A signed tool manifest (<c>zxv_w4_manifest</c>): up to 16 tools with a
/// price, a rate limit and traits. Signing (<see cref="Web4Agent.Sign"/>)
/// fixes the agent and peer ids; adding a tool afterwards drops the
/// signature. Holds only its own memory; calls are serialized by a managed lock.
/// </summary>
public sealed class ToolManifest : IDisposable
{
    /// <summary>Most tools (<c>ZXV_W4_MAX_TOOLS</c>).</summary>
    public const int MaxTools = 16;

    /// <summary>Largest encoded manifest (<c>ZXV_W4_MANIFEST_MAX</c>).</summary>
    public const int MaxEncodedBytes = 4349;

    private readonly ZxvW4ManifestHandle _handle;
    private readonly object _gate = new();

    /// <summary>Creates an empty, unsigned manifest valid in [<paramref name="issuedMs"/>, <paramref name="expiresMs"/>).</summary>
    /// <param name="version">The manifest version.</param>
    /// <param name="issuedMs">Valid from (Unix milliseconds).</param>
    /// <param name="expiresMs">Valid until; above <paramref name="issuedMs"/>.</param>
    public ToolManifest(uint version, ulong issuedMs, ulong expiresMs)
    {
        int rc = NativeMethods.W4ManifestCreate(version, issuedMs, expiresMs, out _handle);
        if (rc != (int)ZxvStatus.Ok)
        {
            _handle.Dispose();
            NativeCall.Check(rc, "ToolManifest.Create");
        }
    }

    private ToolManifest(ZxvW4ManifestHandle handle)
    {
        _handle = handle;
    }

    /// <summary>Gets the header fields.</summary>
    public unsafe ManifestInfo Info
    {
        get
        {
            var id = new byte[Web4Agent.IdBytes];
            var peer = new byte[Web4Agent.IdBytes];
            lock (_gate)
            {
                Enter();
                fixed (byte* a = id)
                fixed (byte* p = peer)
                {
                    NativeCall.Check(NativeMethods.W4ManifestInfo(_handle, a, p, out uint v, out ulong iss, out ulong exp, out uint n, out int signed), "ToolManifest.Info");
                    return new ManifestInfo(id, peer, v, iss, exp, (int)n, signed != 0);
                }
            }
        }
    }

    /// <summary>Gets every tool, in order.</summary>
    public IReadOnlyList<ToolInfo> Tools
    {
        get
        {
            int n = Info.ToolCount;
            var list = new ToolInfo[n];
            for (int i = 0; i < n; i++)
            {
                list[i] = GetTool(i);
            }

            return list;
        }
    }

    /// <summary>Parses the wire form. The signature is kept but NOT checked: call <see cref="Verify"/>.</summary>
    /// <param name="bytes">The encoded manifest.</param>
    /// <returns>A new manifest; dispose it when done.</returns>
    /// <exception cref="ZxvUsageException">Malformed (<see cref="ZxvStatus.InvalidFormat"/>).</exception>
    public static unsafe ToolManifest Decode(ReadOnlySpan<byte> bytes)
    {
        ZxvW4ManifestHandle h;
        int rc;
        fixed (byte* p = bytes)
        {
            rc = NativeMethods.W4ManifestDecode(p, (nuint)bytes.Length, out h);
        }

        if (rc != (int)ZxvStatus.Ok)
        {
            h.Dispose();
            NativeCall.Check(rc, "ToolManifest.Decode");
        }

        return new ToolManifest(h);
    }

    /// <summary>Adds a tool (drops any signature).</summary>
    /// <param name="name">1..31 characters of [a-z0-9._-].</param>
    /// <param name="priceVfv">Price in VFV minor units.</param>
    /// <param name="ratePerMinute">At least 1.</param>
    /// <param name="burst">At least 1.</param>
    /// <param name="traits">What needs care.</param>
    /// <param name="maxInput">At most <see cref="Web4Agent.MaxPayloadBytes"/>.</param>
    /// <exception cref="ZxvUsageException">A bad field, a name already listed (<see cref="ZxvStatus.Duplicate"/>), or 16 tools already.</exception>
    public unsafe void AddTool(string name, ulong priceVfv, uint ratePerMinute, uint burst, ToolTraits traits, uint maxInput)
    {
        byte[] n = NativeBuffers.Utf8Z(name, nameof(name), allowEmpty: true);
        lock (_gate)
        {
            Enter();
            fixed (byte* p = n)
            {
                NativeCall.Check(NativeMethods.W4ManifestAddTool(_handle, p, priceVfv, ratePerMinute, burst, (uint)traits, maxInput), "ToolManifest.AddTool");
            }
        }
    }

    /// <summary>Tool <paramref name="index"/>.</summary>
    /// <param name="index">0-based.</param>
    /// <returns>The tool.</returns>
    /// <exception cref="ZxvUsageException">Past the last tool (<see cref="ZxvStatus.NotFound"/>).</exception>
    public unsafe ToolInfo GetTool(int index)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        byte* name = stackalloc byte[32];
        lock (_gate)
        {
            Enter();
            NativeCall.Check(NativeMethods.W4ManifestTool(_handle, (uint)index, name, out ulong price, out uint rate, out uint burst, out uint flags, out uint max), "ToolManifest.GetTool");
            return new ToolInfo(NativeBuffers.FromZ(new ReadOnlySpan<byte>(name, 32)), price, rate, burst, (ToolTraits)flags, max);
        }
    }

    /// <summary>The index of the tool named <paramref name="name"/>, or -1 when it is not listed.</summary>
    /// <param name="name">The tool name.</param>
    /// <returns>The index or -1.</returns>
    public unsafe int IndexOf(string name)
    {
        byte[] n = NativeBuffers.Utf8Z(name, nameof(name), allowEmpty: true);
        lock (_gate)
        {
            Enter();
            fixed (byte* p = n)
            {
                int rc = NativeMethods.W4ManifestFind(_handle, p, out uint index);
                if (rc == (int)ZxvStatus.NotFound)
                {
                    return -1;
                }

                NativeCall.Check(rc, "ToolManifest.IndexOf");
                return (int)index;
            }
        }
    }

    /// <summary>Checks that the manifest is signed by <paramref name="publicKey"/>, that key is its agent, and it is valid at <paramref name="nowMs"/>.</summary>
    /// <param name="publicKey">The agent's public key (<see cref="Web4Agent.PublicKeyBytes"/> bytes).</param>
    /// <param name="nowMs">The time (Unix milliseconds).</param>
    /// <exception cref="VerificationFailedException">Wrong key, or a bad or missing signature.</exception>
    /// <exception cref="ZxvException">A valid signature but not valid at <paramref name="nowMs"/> (<see cref="ZxvStatus.InvalidState"/>).</exception>
    public unsafe void Verify(ReadOnlySpan<byte> publicKey, ulong nowMs)
    {
        NativeBuffers.RequireLength(publicKey, Web4Agent.PublicKeyBytes, nameof(publicKey));
        lock (_gate)
        {
            Enter();
            fixed (byte* k = publicKey)
            {
                NativeCall.Check(NativeMethods.W4ManifestVerify(_handle, k, nowMs), "ToolManifest.Verify");
            }
        }
    }

    /// <summary>The signed wire form.</summary>
    /// <returns>The bytes.</returns>
    /// <exception cref="ZxvException">Not signed (<see cref="ZxvStatus.InvalidState"/>).</exception>
    public unsafe byte[] Encode()
    {
        lock (_gate)
        {
            Enter();
            return NativeBuffers.ReadBytes((byte* b, nuint c, out nuint l) => NativeMethods.W4ManifestEncode(_handle, b, c, out l), rc => NativeCall.Check(rc, "ToolManifest.Encode"));
        }
    }

    /// <summary>The Web 2 discovery face (for example <c>/.well-known/web4-agent.json</c>) as JSON; prices stay integers in minor units.</summary>
    /// <returns>The JSON text.</returns>
    public unsafe string ToJson()
    {
        lock (_gate)
        {
            Enter();
            return NativeBuffers.ReadText((byte* b, nuint c, out nuint l) => NativeMethods.W4ManifestToJson(_handle, b, c, out l), rc => NativeCall.Check(rc, "ToolManifest.ToJson"));
        }
    }

    /// <summary>Releases the manifest.</summary>
    public void Dispose()
    {
        lock (_gate)
        {
            _handle.Dispose();
        }
    }

    internal void SignWith(Capability capability, ZxvW4AgentHandle agent)
    {
        lock (_gate)
        {
            Enter();
            int rc = NativeMethods.W4ManifestSign(capability.Handle, agent, _handle);
            if (rc != (int)ZxvStatus.Ok)
            {
                NativeCall.Check(rc, "ToolManifest.Sign", capability.Context);
            }
        }
    }

    private void Enter() => ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
}

/// <summary>Consent: the action digest a token must carry, and reading tokens (ABI 1.5). Pure.</summary>
/// <remarks>
/// An agent never moves money or shares personal data without a token signed
/// by the HUMAN's key; the action digest binds the exact tool, parties,
/// request id and payload. Tokens are single-use (<see cref="ConsentLog"/>).
/// </remarks>
public static class Consent
{
    /// <summary>The digest a consent token for one request must carry: SHA3-256 over the tool, from, to, request id and payload.</summary>
    /// <param name="tool">A valid tool name.</param>
    /// <param name="from">The requesting agent (32 bytes).</param>
    /// <param name="to">The serving agent (32 bytes).</param>
    /// <param name="requestId">The request id.</param>
    /// <param name="payload">At most <see cref="Web4Agent.MaxPayloadBytes"/> bytes.</param>
    /// <returns>32 bytes.</returns>
    public static unsafe byte[] ActionDigest(string tool, ReadOnlySpan<byte> from, ReadOnlySpan<byte> to, ulong requestId, ReadOnlySpan<byte> payload)
    {
        byte[] t = NativeBuffers.Utf8Z(tool, nameof(tool), allowEmpty: true);
        NativeBuffers.RequireLength(from, Web4Agent.IdBytes, nameof(from));
        NativeBuffers.RequireLength(to, Web4Agent.IdBytes, nameof(to));
        var digest = new byte[32];
        fixed (byte* pt = t)
        fixed (byte* pf = from)
        fixed (byte* pto = to)
        fixed (byte* pp = payload)
        fixed (byte* d = digest)
        {
            NativeCall.Check(NativeMethods.W4ActionDigest(pt, pf, pto, requestId, pp, (nuint)payload.Length, d), "Consent.ActionDigest");
        }

        return digest;
    }

    internal static unsafe ConsentInfo Read(byte[] token)
    {
        var human = new byte[32];
        var agent = new byte[32];
        var action = new byte[32];
        var nonce = new byte[16];
        fixed (byte* t = token)
        fixed (byte* h = human)
        fixed (byte* a = agent)
        fixed (byte* d = action)
        fixed (byte* n = nonce)
        {
            NativeCall.Check(NativeMethods.W4ConsentInfo(t, h, a, d, out uint scope, out ulong max, out ulong iss, out ulong exp, n), "ConsentToken.Info");
            return new ConsentInfo(human, agent, action, (ConsentScope)scope, max, iss, exp, nonce);
        }
    }
}

/// <summary>
/// A single-use log of consumed and revoked consent tokens
/// (<c>zxv_w4_consent_log</c>). Holds only its own memory; calls are
/// serialized by a managed lock.
/// </summary>
public sealed class ConsentLog : IDisposable
{
    /// <summary>Most entries (<c>ZXV_W4_CONSENT_LOG_MAX</c>).</summary>
    public const int MaxCapacity = 65536;

    private readonly ZxvW4ConsentLogHandle _handle;
    private readonly object _gate = new();

    /// <summary>Creates an empty log.</summary>
    /// <param name="capacity">1..<see cref="MaxCapacity"/>.</param>
    public ConsentLog(int capacity)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(capacity);
        int rc = NativeMethods.W4ConsentLogCreate((uint)capacity, out _handle);
        if (rc != (int)ZxvStatus.Ok)
        {
            _handle.Dispose();
            NativeCall.Check(rc, "ConsentLog.Create");
        }
    }

    /// <summary>
    /// Checks that <paramref name="token"/> lets <paramref name="agentId"/> do
    /// <paramref name="action"/> with scope <paramref name="need"/> for
    /// <paramref name="amount"/>, signed by the human whose key is
    /// <paramref name="humanPublicKey"/> and valid at <paramref name="nowMs"/>;
    /// then records it as used. Returning normally means consent was given
    /// and is now spent.
    /// </summary>
    /// <param name="token">The token, or <c>default</c> for none (always refused).</param>
    /// <param name="humanPublicKey">The human's public key.</param>
    /// <param name="agentId">The acting agent.</param>
    /// <param name="action">The action digest.</param>
    /// <param name="need">The scope needed (not <see cref="ConsentScope.None"/>).</param>
    /// <param name="amount">VFV minor units (checked when <paramref name="need"/> has <see cref="ConsentScope.Money"/>).</param>
    /// <param name="nowMs">The time (Unix milliseconds).</param>
    /// <exception cref="ZxvRefusedException">No consent (<see cref="ZxvStatus.ConsentRequired"/>): missing, a mismatch, expired, a failing signature, or already used or revoked; the reason is in the message.</exception>
    /// <exception cref="ZxvException">Malformed token, or the log is full even after pruning (<see cref="ZxvStatus.CapacityExceeded"/>: fail closed).</exception>
    public unsafe void Check(ConsentToken token, ReadOnlySpan<byte> humanPublicKey, ReadOnlySpan<byte> agentId, ReadOnlySpan<byte> action, ConsentScope need, ulong amount, ulong nowMs)
    {
        NativeBuffers.RequireLength(humanPublicKey, Web4Agent.PublicKeyBytes, nameof(humanPublicKey));
        NativeBuffers.RequireLength(agentId, Web4Agent.IdBytes, nameof(agentId));
        NativeBuffers.RequireLength(action, 32, nameof(action));
        byte[]? t = token.IsEmpty ? null : token.RequireBytes();
        lock (_gate)
        {
            Enter();
            fixed (byte* pt = t)
            fixed (byte* k = humanPublicKey)
            fixed (byte* a = agentId)
            fixed (byte* d = action)
            {
                NativeCall.Check(NativeMethods.W4ConsentCheck(_handle, pt, k, a, d, (uint)need, amount, nowMs), "ConsentLog.Check");
            }
        }
    }

    /// <summary>Revokes a token before use; later checks of it are refused. Revoking twice is fine.</summary>
    /// <param name="token">The token.</param>
    /// <param name="nowMs">The time (Unix milliseconds).</param>
    public unsafe void Revoke(ConsentToken token, ulong nowMs)
    {
        byte[] t = token.RequireBytes();
        lock (_gate)
        {
            Enter();
            fixed (byte* pt = t)
            {
                NativeCall.Check(NativeMethods.W4ConsentRevoke(_handle, pt, nowMs), "ConsentLog.Revoke");
            }
        }
    }

    /// <summary>Drops entries whose token expired at or before <paramref name="nowMs"/>.</summary>
    /// <param name="nowMs">The time (Unix milliseconds).</param>
    /// <returns>The entries left.</returns>
    public int Prune(ulong nowMs)
    {
        lock (_gate)
        {
            Enter();
            NativeCall.Check(NativeMethods.W4ConsentLogPrune(_handle, nowMs, out uint remaining), "ConsentLog.Prune");
            return (int)remaining;
        }
    }

    /// <summary>Releases the log.</summary>
    public void Dispose()
    {
        lock (_gate)
        {
            _handle.Dispose();
        }
    }

    private void Enter() => ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
}

/// <summary>Signed envelopes (ABI 1.5, <c>zxv_w4_env_*</c>): reading and verifying. Seal with <see cref="Web4Agent.Seal"/>. Pure.</summary>
/// <remarks>Envelopes are signed, not encrypted. Replay, timestamp and rate checks are the receiver's job.</remarks>
public static class Envelope
{
    /// <summary>Parses an envelope (no signature check).</summary>
    /// <param name="envelope">The bytes.</param>
    /// <returns>Its fields.</returns>
    /// <exception cref="ZxvUsageException">Malformed (<see cref="ZxvStatus.InvalidFormat"/>).</exception>
    public static unsafe EnvelopeInfo Decode(ReadOnlySpan<byte> envelope)
    {
        var from = new byte[32];
        var to = new byte[32];
        byte* tool = stackalloc byte[32];
        fixed (byte* e = envelope)
        fixed (byte* f = from)
        fixed (byte* t = to)
        {
            NativeCall.Check(
                NativeMethods.W4EnvDecode(e, (nuint)envelope.Length, out int kind, out uint flags, f, t, out ulong seq, out ulong ts, out ulong req, out uint chunk, tool, out uint plen),
                "Envelope.Decode");
            return new EnvelopeInfo((EnvelopeKind)kind, (EnvelopeMarks)flags, from, to, seq, ts, req, chunk, NativeBuffers.FromZ(new ReadOnlySpan<byte>(tool, 32)), (int)plen);
        }
    }

    /// <summary>The payload.</summary>
    /// <param name="envelope">The bytes.</param>
    /// <returns>The payload.</returns>
    public static unsafe byte[] Payload(ReadOnlySpan<byte> envelope)
    {
        fixed (byte* e = envelope)
        {
            byte* x = e;
            nuint n = (nuint)envelope.Length;
            return NativeBuffers.ReadBytes((byte* b, nuint c, out nuint l) => NativeMethods.W4EnvPayload(x, n, b, c, out l), rc => NativeCall.Check(rc, "Envelope.Payload"));
        }
    }

    /// <summary>The attached consent token, or <c>default</c> when none is attached.</summary>
    /// <param name="envelope">The bytes.</param>
    /// <returns>The token, or <c>default</c>.</returns>
    public static unsafe ConsentToken AttachedConsent(ReadOnlySpan<byte> envelope)
    {
        var token = new byte[ConsentToken.Length];
        fixed (byte* e = envelope)
        fixed (byte* t = token)
        {
            int rc = NativeMethods.W4EnvConsent(e, (nuint)envelope.Length, t);
            if (rc == (int)ZxvStatus.NotFound)
            {
                return default;
            }

            NativeCall.Check(rc, "Envelope.AttachedConsent");
        }

        return new ConsentToken(token);
    }

    /// <summary>Whether <paramref name="senderPublicKey"/> is the envelope's sender and its signature verifies.</summary>
    /// <param name="envelope">The bytes.</param>
    /// <param name="senderPublicKey">The sender's public key.</param>
    /// <returns><see langword="true"/> when it verifies; <see langword="false"/> on <see cref="ZxvStatus.VerificationFailed"/>.</returns>
    public static unsafe bool Verify(ReadOnlySpan<byte> envelope, ReadOnlySpan<byte> senderPublicKey)
    {
        NativeBuffers.RequireLength(senderPublicKey, Web4Agent.PublicKeyBytes, nameof(senderPublicKey));
        fixed (byte* e = envelope)
        fixed (byte* k = senderPublicKey)
        {
            int rc = NativeMethods.W4EnvVerify(e, (nuint)envelope.Length, k);
            if (rc == (int)ZxvStatus.VerificationFailed)
            {
                return false;
            }

            NativeCall.Check(rc, "Envelope.Verify");
            return true;
        }
    }
}
