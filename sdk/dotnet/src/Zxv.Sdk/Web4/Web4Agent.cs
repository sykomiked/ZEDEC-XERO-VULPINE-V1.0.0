// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using Zxv.Interop;

namespace Zxv.Web4;

/// <summary>
/// A Web 4 key handle (ABI 1.5, <c>zxv_w4_agent</c>): an ML-DSA-65 key pair
/// and the agent's signed card. Used for agents and for humans (a human's id
/// is the handle's <see cref="AgentId"/>). Create one with
/// <see cref="ZxvPlatform.CreateWeb4Agent(Capability, string, ulong, ReadOnlySpan{byte}, Web4Agent?)"/>.
/// </summary>
/// <remarks>
/// <para>
/// SECRETS. The private key never leaves the native handle: no member returns
/// it, in bytes or text, and <see cref="Dispose"/> wipes it. Signing
/// (manifests, envelopes) needs <see cref="CapabilityRights.CryptoSign"/>
/// and issuing consent needs <see cref="CapabilityRights.Attest"/>, both
/// from the capability the handle was created with. Calls are serialized by
/// a managed lock (sealing advances the envelope sequence). Once the platform
/// halts, every call raises <see cref="ZxvFatalException"/>.
/// </para>
/// <para>
/// HONEST LIMITS. Envelopes are signed, not encrypted. Replay windows, rate
/// limits, carriage over freight and the Web 2 and Web 3 bridges are not in
/// this ABI. Interoperability is between ZXV instances only; no other agent
/// protocol is implemented or claimed. There is no clock inside: every call
/// that needs the time takes it from the caller in Unix milliseconds.
/// </para>
/// </remarks>
public sealed class Web4Agent : IDisposable
{
    /// <summary>ML-DSA-65 public key length (<c>ZXV_W4_PK_BYTES</c>).</summary>
    public const int PublicKeyBytes = 1952;

    /// <summary>ML-DSA-65 signature length (<c>ZXV_W4_SIG_BYTES</c>).</summary>
    public const int SignatureBytes = 3309;

    /// <summary>Agent, peer and human id length (<c>ZXV_W4_ID_BYTES</c>).</summary>
    public const int IdBytes = 32;

    /// <summary>Longest agent or tool name (<c>ZXV_W4_NAME_MAX</c>).</summary>
    public const int MaxNameLength = 31;

    /// <summary>Largest envelope payload (<c>ZXV_W4_MSG_MAX_PAYLOAD</c>).</summary>
    public const int MaxPayloadBytes = 2048;

    /// <summary>Largest node-bound card (<c>ZXV_W4_CARD_MAX</c>).</summary>
    public const int MaxCardBytes = 7259;

    /// <summary>Largest envelope (<c>ZXV_W4_ENV_MAX</c>).</summary>
    public const int MaxEnvelopeBytes = 8970;

    private readonly Capability _cap;
    private readonly ZxvW4AgentHandle _handle;
    private readonly object _gate = new();

    internal unsafe Web4Agent(Capability capability, string name, ulong nowMs, ReadOnlySpan<byte> seed, Web4Agent? node)
    {
        byte[] n = NativeBuffers.Utf8Z(name, nameof(name), allowEmpty: true);
        if (!seed.IsEmpty)
        {
            NativeBuffers.RequireLength(seed, 32, nameof(seed));
        }

        _cap = capability;
        capability.Enter("Web4Agent.Create");
        int rc;
        bool added = false;
        try
        {
            IntPtr nodePtr = IntPtr.Zero;
            if (node is not null)
            {
                ObjectDisposedException.ThrowIf(node._handle.IsClosed, node);
                node._handle.DangerousAddRef(ref added);
                nodePtr = node._handle.DangerousGetHandle();
            }

            fixed (byte* s = seed)
            fixed (byte* pn = n)
            {
                rc = NativeMethods.W4AgentCreate(capability.Handle, seed.IsEmpty ? null : s, pn, nodePtr, nowMs, out _handle);
            }
        }
        finally
        {
            if (added)
            {
                node!._handle.DangerousRelease();
            }
        }

        if (rc != (int)ZxvStatus.Ok)
        {
            _handle.Dispose();
            capability.Check(rc, "Web4Agent.Create");
        }

        _handle.AddDependency(capability.Context.Handle);
    }

    /// <summary>Gets the agent id: SHA3-256 of the public key (32 bytes).</summary>
    public byte[] AgentId => Ids().AgentId;

    /// <summary>Gets the peer id: SHA3-256 of the hosting node's public key (the agent id when self-bound).</summary>
    public byte[] PeerId => Ids().PeerId;

    /// <summary>Gets the ML-DSA-65 public key (<see cref="PublicKeyBytes"/> bytes).</summary>
    public unsafe byte[] PublicKey
    {
        get
        {
            var pk = new byte[PublicKeyBytes];
            lock (_gate)
            {
                Read("Web4Agent.PublicKey");
                fixed (byte* p = pk)
                {
                    Check(NativeMethods.W4AgentInfo(_handle, null, null, p), "Web4Agent.PublicKey");
                }
            }

            return pk;
        }
    }

    /// <summary>Gets the agent's card: what a peer needs to verify it (<see cref="AgentCard.Verify"/>).</summary>
    public unsafe byte[] Card
    {
        get
        {
            lock (_gate)
            {
                Read("Web4Agent.Card");
                return NativeBuffers.ReadBytes((byte* b, nuint c, out nuint l) => NativeMethods.W4AgentCard(_handle, b, c, out l), rc => Check(rc, "Web4Agent.Card"));
            }
        }
    }

    /// <summary>Signs <paramref name="manifest"/> with this agent's key; it fixes the manifest's agent and peer ids. Needs <see cref="CapabilityRights.CryptoSign"/>.</summary>
    /// <param name="manifest">The manifest.</param>
    public void Sign(ToolManifest manifest)
    {
        ArgumentNullException.ThrowIfNull(manifest);
        lock (_gate)
        {
            Write("Web4Agent.Sign");
            manifest.SignWith(_cap, _handle);
        }
    }

    /// <summary>
    /// Issues a consent token on the HUMAN's side, after showing them the
    /// action: signed by this handle (the human) for <paramref name="agentId"/>
    /// to do <paramref name="action"/>. Needs <see cref="CapabilityRights.Attest"/>.
    /// </summary>
    /// <param name="agentId">The agent allowed to act (32 bytes).</param>
    /// <param name="action">The action digest (<see cref="Consent.ActionDigest"/>, 32 bytes).</param>
    /// <param name="scope">What it allows (not <see cref="ConsentScope.None"/>).</param>
    /// <param name="maxVfv">Most VFV minor units it allows.</param>
    /// <param name="issuedMs">Valid from (Unix milliseconds).</param>
    /// <param name="expiresMs">Valid until, exclusive; above <paramref name="issuedMs"/>.</param>
    /// <param name="nonce">16 bytes, or empty for a random nonce.</param>
    /// <returns>The token.</returns>
    public unsafe ConsentToken IssueConsent(ReadOnlySpan<byte> agentId, ReadOnlySpan<byte> action, ConsentScope scope, ulong maxVfv, ulong issuedMs, ulong expiresMs, ReadOnlySpan<byte> nonce = default)
    {
        NativeBuffers.RequireLength(agentId, IdBytes, nameof(agentId));
        NativeBuffers.RequireLength(action, 32, nameof(action));
        if (!nonce.IsEmpty)
        {
            NativeBuffers.RequireLength(nonce, 16, nameof(nonce));
        }

        var token = new byte[ConsentToken.Length];
        lock (_gate)
        {
            Write("Web4Agent.IssueConsent");
            fixed (byte* a = agentId)
            fixed (byte* d = action)
            fixed (byte* pn = nonce)
            fixed (byte* t = token)
            {
                Check(NativeMethods.W4ConsentIssue(_cap.Handle, _handle, a, d, (uint)scope, maxVfv, issuedMs, expiresMs, nonce.IsEmpty ? null : pn, t), "Web4Agent.IssueConsent");
            }
        }

        return new ConsentToken(token);
    }

    /// <summary>
    /// Builds, signs and encodes an envelope from this agent to
    /// <paramref name="to"/>, using its next sequence number. Needs
    /// <see cref="CapabilityRights.CryptoSign"/>.
    /// </summary>
    /// <param name="kind">The kind.</param>
    /// <param name="to">The receiver's agent id (32 bytes).</param>
    /// <param name="requestId">The request id.</param>
    /// <param name="chunk">The chunk index.</param>
    /// <param name="tool">A valid tool name.</param>
    /// <param name="marks"><see cref="EnvelopeMarks.None"/> or <see cref="EnvelopeMarks.Final"/>.</param>
    /// <param name="payload">At most <see cref="MaxPayloadBytes"/> bytes.</param>
    /// <param name="nowMs">The time (Unix milliseconds).</param>
    /// <param name="consent">A token to attach, or <c>default</c> for none.</param>
    /// <returns>The envelope bytes.</returns>
    public unsafe byte[] Seal(EnvelopeKind kind, ReadOnlySpan<byte> to, ulong requestId, uint chunk, string tool, EnvelopeMarks marks, ReadOnlySpan<byte> payload, ulong nowMs, ConsentToken consent = default)
    {
        NativeBuffers.RequireLength(to, IdBytes, nameof(to));
        byte[] t = NativeBuffers.Utf8Z(tool, nameof(tool), allowEmpty: true);
        byte[]? tok = consent.IsEmpty ? null : consent.RequireBytes();
        lock (_gate)
        {
            Write("Web4Agent.Seal");
            fixed (byte* pto = to)
            fixed (byte* pt = t)
            fixed (byte* pp = payload)
            fixed (byte* pk = tok)
            {
                byte* a = pto;
                byte* b = pt;
                byte* c = pp;
                byte* d = pk;
                nuint n = (nuint)payload.Length;
                return NativeBuffers.ReadBytes(
                    (byte* buf, nuint cap, out nuint len) => NativeMethods.W4EnvSeal(_cap.Handle, _handle, (int)kind, a, requestId, chunk, b, (uint)marks, c, n, d, nowMs, buf, cap, out len),
                    rc => Check(rc, "Web4Agent.Seal"));
            }
        }
    }

    /// <summary>
    /// Seals a reply to a REQUEST envelope addressed to this agent: from and
    /// to swapped, same request id and tool. The request's signature is not
    /// checked here (<see cref="Envelope.Verify"/>). Needs <see cref="CapabilityRights.CryptoSign"/>.
    /// </summary>
    /// <param name="request">The request envelope.</param>
    /// <param name="kind"><see cref="EnvelopeKind.Response"/>, <see cref="EnvelopeKind.Stream"/> or <see cref="EnvelopeKind.Error"/>.</param>
    /// <param name="chunk">The chunk index.</param>
    /// <param name="marks"><see cref="EnvelopeMarks.None"/> or <see cref="EnvelopeMarks.Final"/>.</param>
    /// <param name="payload">At most <see cref="MaxPayloadBytes"/> bytes.</param>
    /// <param name="nowMs">The time (Unix milliseconds).</param>
    /// <returns>The reply envelope.</returns>
    public unsafe byte[] Reply(ReadOnlySpan<byte> request, EnvelopeKind kind, uint chunk, EnvelopeMarks marks, ReadOnlySpan<byte> payload, ulong nowMs)
    {
        lock (_gate)
        {
            Write("Web4Agent.Reply");
            fixed (byte* pr = request)
            fixed (byte* pp = payload)
            {
                byte* a = pr;
                byte* c = pp;
                nuint rn = (nuint)request.Length;
                nuint n = (nuint)payload.Length;
                return NativeBuffers.ReadBytes(
                    (byte* buf, nuint cap, out nuint len) => NativeMethods.W4EnvReply(_cap.Handle, _handle, a, rn, (int)kind, chunk, (uint)marks, c, n, nowMs, buf, cap, out len),
                    rc => Check(rc, "Web4Agent.Reply"));
            }
        }
    }

    /// <summary>Wipes the private key and releases the handle.</summary>
    public void Dispose()
    {
        lock (_gate)
        {
            _handle.Dispose();
        }
    }

    /// <summary>A description that never includes key material.</summary>
    /// <returns>The agent id in hex.</returns>
    public override string ToString() => _handle.IsClosed ? "Web4Agent(disposed)" : $"Web4Agent({Convert.ToHexString(AgentId).ToLowerInvariant()})";

    private unsafe (byte[] AgentId, byte[] PeerId) Ids()
    {
        var id = new byte[IdBytes];
        var peer = new byte[IdBytes];
        lock (_gate)
        {
            Read("Web4Agent.Info");
            fixed (byte* a = id)
            fixed (byte* p = peer)
            {
                Check(NativeMethods.W4AgentInfo(_handle, a, p, null), "Web4Agent.Info");
            }
        }

        return (id, peer);
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
