// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.web4;

import static java.lang.foreign.ValueLayout.JAVA_BYTE;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import org.zedec.zxv.Capability;
import org.zedec.zxv.ZxvContext;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Caps;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native15;
import org.zedec.zxv.internal.NativeResource;

/**
 * An ML-DSA-65 key pair and the agent's signed card ({@code zxv_w4_agent}),
 * for an agent or for a human (whose human_id is this key's agent id).
 *
 * <p>SECRETS. The private key is generated inside the native handle, used
 * there for signing, and wiped by {@link #close()}. No method returns it, in
 * bytes or text, and {@link #toString()} shows only the public agent id.
 *
 * <p>CAPABILITIES. The key keeps the capability it was created with and
 * signs through it: creating, cards, manifests and envelopes need
 * {@code CRYPTO_SIGN}; {@link #issueConsent} needs {@code ATTEST}. Every
 * call fails closed once the key's context halts.
 *
 * <p>Threading: the C handle is not thread-safe (sealing advances its
 * envelope sequence), so every method here is {@code synchronized}.
 */
public final class AgentKey implements AutoCloseable {
    private final Capability cap;
    private final ZxvContext ctx;
    private final NativeResource res;
    private final byte[] agentId, peerId, publicKey;

    private AgentKey(Capability cap, ZxvContext ctx, MemorySegment h) {
        this.cap = cap;
        this.ctx = ctx;
        this.res = new NativeResource("web4 agent key", h, x -> Ffi.callVoid(Native15.w4_agent_destroy, x), ctx,
                Caps.res(ctx));
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment id = a.allocate(32), peer = a.allocate(32), pk = a.allocate(Web4.PUBLIC_KEY_BYTES);
            Ffi.check(Ffi.call(Native15.w4_agent_info, l.h(0), id, peer, pk), ctx);
            agentId = id.toArray(JAVA_BYTE);
            peerId = peer.toArray(JAVA_BYTE);
            publicKey = pk.toArray(JAVA_BYTE);
        }
    }

    /**
     * Creates a key and signs the agent's binding.
     *
     * @param cap   a capability with {@code CRYPTO_SIGN} (and {@code ATTEST} to issue consent)
     * @param seed  32 bytes for a reproducible key, or null for a fresh one
     * @param name  1..31 characters of [a-z0-9._-]
     * @param node  the key of the node hosting this agent (it signs the binding;
     *              same context), or null for a self-bound agent
     * @param nowMs the binding time, Unix milliseconds
     * @throws org.zedec.zxv.CapabilityDeniedException without {@code CRYPTO_SIGN}
     */
    public static AgentKey create(Capability cap, byte[] seed, String name, AgentKey node, long nowMs) {
        ZxvContext ctx = Caps.ctx(cap);
        Buf.nonNull("name", name);
        try (Lease l = node == null ? Lease.of(Caps.res(cap)) : Lease.of(Caps.res(cap), node.res);
                Arena a = Arena.ofConfined()) {
            MemorySegment n = node == null ? MemorySegment.NULL : l.h(1);
            if (node == null) {
                return new AgentKey(cap, ctx, Buf.create(ctx, Native15.w4_agent_create, l.h(0),
                        Buf.fixedOrNull(a, "seed", seed, 32), Ffi.cstr(a, name), n, nowMs));
            }
            synchronized (node) {
                return new AgentKey(cap, ctx, Buf.create(ctx, Native15.w4_agent_create, l.h(0),
                        Buf.fixedOrNull(a, "seed", seed, 32), Ffi.cstr(a, name), n, nowMs));
            }
        }
    }

    /** The agent id: SHA3-256 of the public key (a copy). */
    public byte[] agentId() {
        return agentId.clone();
    }

    /** The hosting node's id (the agent id when self-bound; a copy). */
    public byte[] peerId() {
        return peerId.clone();
    }

    /** The ML-DSA-65 public key (1952 bytes, a copy). */
    public byte[] publicKey() {
        return publicKey.clone();
    }

    /** The agent's signed card. */
    public synchronized AgentCard card() {
        try (Lease l = Lease.of(res)) {
            MemorySegment h = l.h(0);
            return AgentCard.wrap(Buf.r4(ctx, Web4.CARD_MAX, (buf, n, len) -> Ffi.call(Native15.w4_agent_card, h, buf, n, len)));
        }
    }

    /**
     * Builds, signs and encodes an envelope to {@code to}, using this key's
     * next sequence number.
     *
     * @param kind      the kind
     * @param to        the receiver's agent id (32 bytes)
     * @param requestId the request id (unsigned)
     * @param chunk     the chunk index (uint32 bits)
     * @param tool      a valid tool name
     * @param flags     0 or {@link Envelope#FLAG_FINAL}
     * @param payload   at most {@link Web4#MAX_PAYLOAD} bytes
     * @param consent   a token to attach, or null
     * @param nowMs     the timestamp, Unix milliseconds
     */
    public synchronized Envelope seal(EnvelopeKind kind, byte[] to, long requestId, int chunk, String tool, int flags,
            byte[] payload, ConsentToken consent, long nowMs) {
        Buf.nonNull("kind", kind);
        Buf.nonNull("tool", tool);
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment t = Buf.fixed(a, "to", to, Web4.ID_BYTES), tl = Ffi.cstr(a, tool), p = Buf.in(a, payload),
                    c = consent == null ? MemorySegment.NULL : Buf.in(a, consent.raw());
            long pl = payload == null ? 0 : payload.length;
            return Envelope.wrap(Buf.r4(ctx, Web4.ENVELOPE_MAX, (buf, n, len) -> Ffi.call(Native15.w4_env_seal, l.h(0),
                    l.h(1), kind.code(), t, requestId, chunk, tl, flags, p, pl, c, nowMs, buf, n, len)));
        }
    }

    /** Seals a REQUEST with no consent token and chunk 0. */
    public Envelope request(byte[] to, long requestId, String tool, byte[] payload, long nowMs) {
        return seal(EnvelopeKind.REQUEST, to, requestId, 0, tool, 0, payload, null, nowMs);
    }

    /**
     * Seals a reply to a REQUEST addressed to this agent: to and from
     * swapped, same request id and tool. The request's signature is not
     * checked here ({@link Envelope#verify}).
     *
     * @param kind RESPONSE, STREAM or ERROR
     */
    public synchronized Envelope reply(Envelope request, EnvelopeKind kind, int chunk, int flags, byte[] payload,
            long nowMs) {
        Buf.nonNull("request", request);
        Buf.nonNull("kind", kind);
        byte[] r = request.raw();
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment rq = Buf.in(a, r), p = Buf.in(a, payload);
            long pl = payload == null ? 0 : payload.length;
            return Envelope.wrap(Buf.r4(ctx, Web4.ENVELOPE_MAX, (buf, n, len) -> Ffi.call(Native15.w4_env_reply, l.h(0),
                    l.h(1), rq, (long) r.length, kind.code(), chunk, flags, p, pl, nowMs, buf, n, len)));
        }
    }

    /**
     * Signs this key's manifest: fixes its agent and peer ids. Needs {@code CRYPTO_SIGN}.
     */
    public void sign(ToolManifest manifest) {
        Buf.nonNull("manifest", manifest);
        manifest.signWith(this);
    }

    synchronized void signManifest(NativeResource manifest) {
        try (Lease l = Lease.of(Caps.res(cap), res, manifest)) {
            Ffi.check(Ffi.call(Native15.w4_manifest_sign, l.h(0), l.h(1), l.h(2)), ctx);
        }
    }

    /**
     * Issues a consent token on the HUMAN's side, after showing them the
     * action: this key signs for {@code agentId} to do {@code action} within
     * {@code scope}. Needs {@code ATTEST}.
     *
     * @param agentId   the agent allowed to act (32 bytes)
     * @param action    the action digest ({@link Web4#actionDigest}, 32 bytes)
     * @param scope     nonzero {@link ConsentScope} bits
     * @param maxVfv    the most money, in VFV minor units (unsigned)
     * @param issuedMs  start of validity
     * @param expiresMs end of validity (exclusive), after {@code issuedMs}
     * @param nonce     16 bytes, or null for a random one
     * @throws org.zedec.zxv.CapabilityDeniedException without {@code ATTEST}
     */
    public synchronized ConsentToken issueConsent(byte[] agentId, byte[] action, int scope, long maxVfv, long issuedMs,
            long expiresMs, byte[] nonce) {
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(ConsentToken.BYTES);
            Ffi.check(Ffi.call(Native15.w4_consent_issue, l.h(0), l.h(1), Buf.fixed(a, "agentId", agentId, 32),
                    Buf.fixed(a, "action", action, 32), scope, maxVfv, issuedMs, expiresMs,
                    Buf.fixedOrNull(a, "nonce", nonce, 16), out), ctx);
            return ConsentToken.of(out.toArray(JAVA_BYTE));
        }
    }

    /** Whether {@link #close()} has run. */
    public boolean isClosed() {
        return res.isClosed();
    }

    /** Wipes the private key and frees the handle. Idempotent. */
    @Override
    public void close() {
        res.close();
    }

    /** The public agent id in hex; never key material. */
    @Override
    public String toString() {
        return "AgentKey[" + java.util.HexFormat.of().formatHex(agentId) + "]";
    }
}
