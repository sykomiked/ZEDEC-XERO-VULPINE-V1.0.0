// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using Zxv.Interop;

namespace Zxv.Web4;

/// <summary>What a tool does that needs care (<c>ZXV_W4_TOOL_*</c>).</summary>
[Flags]
public enum ToolTraits : uint
{
    /// <summary>Nothing special.</summary>
    None = 0,

    /// <summary>The tool moves money.</summary>
    Money = 0x01,

    /// <summary>The tool reveals personal data.</summary>
    PersonalData = 0x02,

    /// <summary>The tool answers with STREAM chunks.</summary>
    Streaming = 0x04,
}

/// <summary>What a consent token allows (<c>ZXV_W4_CONSENT_*</c>).</summary>
[Flags]
public enum ConsentScope : uint
{
    /// <summary>Nothing (not a valid scope on its own).</summary>
    None = 0,

    /// <summary>Moving money, up to the token's maximum.</summary>
    Money = 0x01,

    /// <summary>Sharing personal data.</summary>
    PersonalData = 0x02,
}

/// <summary>Envelope kinds (<c>ZXV_W4_MSG_*</c>).</summary>
public enum EnvelopeKind
{
    /// <summary>Not a kind.</summary>
    None = 0,

    /// <summary>A request.</summary>
    Request = 1,

    /// <summary>A response.</summary>
    Response = 2,

    /// <summary>One chunk of a streamed answer.</summary>
    Stream = 3,

    /// <summary>Cancel a request.</summary>
    Cancel = 4,

    /// <summary>An error answer.</summary>
    Error = 5,
}

/// <summary>Envelope flags (<c>ZXV_W4_MF_*</c>).</summary>
[Flags]
public enum EnvelopeMarks : uint
{
    /// <summary>None.</summary>
    None = 0,

    /// <summary>The last STREAM chunk, or the final response.</summary>
    Final = 0x01,

    /// <summary>A consent token is attached (set by sealing, never by the caller).</summary>
    Consent = 0x02,
}

/// <summary>What a verified agent card says.</summary>
/// <param name="AgentId">SHA3-256 of the agent's public key.</param>
/// <param name="PeerId">SHA3-256 of the hosting node's public key (the agent id when self-bound).</param>
/// <param name="Name">The agent's name.</param>
/// <param name="BoundMs">When the binding was signed (Unix milliseconds).</param>
public sealed record AgentCardInfo(byte[] AgentId, byte[] PeerId, string Name, ulong BoundMs);

/// <summary>A tool listed in a <see cref="ToolManifest"/>.</summary>
/// <param name="Name">1..31 characters of [a-z0-9._-].</param>
/// <param name="PriceVfv">Price in VFV minor units (2 digits).</param>
/// <param name="RatePerMinute">Calls per minute (at least 1).</param>
/// <param name="Burst">Burst (at least 1).</param>
/// <param name="Traits">What needs care.</param>
/// <param name="MaxInput">Largest input, at most <see cref="Web4Agent.MaxPayloadBytes"/>.</param>
public sealed record ToolInfo(string Name, ulong PriceVfv, uint RatePerMinute, uint Burst, ToolTraits Traits, uint MaxInput);

/// <summary>A manifest's header fields; ids are zero until signed.</summary>
/// <param name="AgentId">The signing agent.</param>
/// <param name="PeerId">The signing agent's node.</param>
/// <param name="Version">The manifest version.</param>
/// <param name="IssuedMs">Valid from (Unix milliseconds).</param>
/// <param name="ExpiresMs">Valid until, exclusive.</param>
/// <param name="ToolCount">Tools listed.</param>
/// <param name="IsSigned">Whether it carries a signature (not whether it verifies).</param>
public sealed record ManifestInfo(byte[] AgentId, byte[] PeerId, uint Version, ulong IssuedMs, ulong ExpiresMs, int ToolCount, bool IsSigned);

/// <summary>A consent token's fields (read without a signature check).</summary>
/// <param name="HumanId">The human's id (their key handle's agent id).</param>
/// <param name="AgentId">The agent allowed to act.</param>
/// <param name="Action">The action digest (<see cref="Consent.ActionDigest"/>).</param>
/// <param name="Scope">What it allows.</param>
/// <param name="MaxVfv">Most VFV minor units it allows.</param>
/// <param name="IssuedMs">Valid from.</param>
/// <param name="ExpiresMs">Valid until, exclusive.</param>
/// <param name="Nonce">16 bytes that make it single-use.</param>
public sealed record ConsentInfo(byte[] HumanId, byte[] AgentId, byte[] Action, ConsentScope Scope, ulong MaxVfv, ulong IssuedMs, ulong ExpiresMs, byte[] Nonce);

/// <summary>An envelope's fields (read without a signature check).</summary>
/// <param name="Kind">The kind.</param>
/// <param name="Marks">The flags.</param>
/// <param name="From">The sender's agent id.</param>
/// <param name="To">The receiver's agent id.</param>
/// <param name="Sequence">The sender's strictly increasing sequence number.</param>
/// <param name="TimestampMs">When it was sealed (Unix milliseconds, caller supplied).</param>
/// <param name="RequestId">The request id.</param>
/// <param name="Chunk">The chunk index.</param>
/// <param name="Tool">The tool name.</param>
/// <param name="PayloadLength">The payload length.</param>
public sealed record EnvelopeInfo(EnvelopeKind Kind, EnvelopeMarks Marks, byte[] From, byte[] To, ulong Sequence, ulong TimestampMs, ulong RequestId, uint Chunk, string Tool, int PayloadLength);

/// <summary>
/// A consent token: <see cref="Length"/> bytes signed by a human's key over
/// {human, agent, action digest, scope, max amount, validity, nonce}. A value
/// type over the encoded bytes.
/// </summary>
/// <remarks>
/// HONEST LIMITS. A token proves that a key holder signed. That the holder is
/// the right human and understood what they signed depends on the device and
/// interface that showed the action, which this library cannot see.
/// </remarks>
public readonly struct ConsentToken : IEquatable<ConsentToken>
{
    /// <summary>The encoded length (<c>ZXV_W4_CONSENT_BYTES</c>).</summary>
    public const int Length = 3453;

    private readonly byte[]? _bytes;

    internal ConsentToken(byte[] bytes)
    {
        _bytes = bytes;
    }

    /// <summary>Gets the encoded bytes (empty for <c>default</c>).</summary>
    public ReadOnlySpan<byte> Bytes => _bytes;

    /// <summary>Gets a value indicating whether this is <c>default</c>.</summary>
    public bool IsEmpty => _bytes is null;

    /// <summary>Gets the token's fields.</summary>
    /// <exception cref="ZxvUsageException">Malformed (<see cref="ZxvStatus.InvalidFormat"/>).</exception>
    public ConsentInfo Info => Consent.Read(RequireBytes());

    /// <summary>Wraps encoded token bytes, checking that they parse.</summary>
    /// <param name="bytes"><see cref="Length"/> bytes.</param>
    /// <returns>The token.</returns>
    /// <exception cref="ZxvUsageException">Malformed (<see cref="ZxvStatus.InvalidFormat"/>).</exception>
    public static ConsentToken FromBytes(ReadOnlySpan<byte> bytes)
    {
        NativeBuffers.RequireLength(bytes, Length, nameof(bytes));
        byte[] copy = bytes.ToArray();
        Consent.Read(copy);
        return new ConsentToken(copy);
    }

    /// <summary>Equality.</summary>
    /// <param name="left">The left value.</param>
    /// <param name="right">The right value.</param>
    /// <returns>Whether they are byte for byte equal.</returns>
    public static bool operator ==(ConsentToken left, ConsentToken right) => left.Equals(right);

    /// <summary>Inequality.</summary>
    /// <param name="left">The left value.</param>
    /// <param name="right">The right value.</param>
    /// <returns>Whether they differ.</returns>
    public static bool operator !=(ConsentToken left, ConsentToken right) => !left.Equals(right);

    /// <summary>A copy of the bytes.</summary>
    /// <returns>The bytes.</returns>
    public byte[] ToArray() => Bytes.ToArray();

    /// <inheritdoc/>
    public bool Equals(ConsentToken other) => Bytes.SequenceEqual(other.Bytes);

    /// <inheritdoc/>
    public override bool Equals(object? obj) => obj is ConsentToken t && Equals(t);

    /// <inheritdoc/>
    public override int GetHashCode()
    {
        var h = default(HashCode);
        h.AddBytes(Bytes);
        return h.ToHashCode();
    }

    /// <inheritdoc/>
    public override string ToString() => $"ConsentToken({Length} bytes)";

    internal byte[] RequireBytes() => _bytes ?? throw new ArgumentException("The consent token is empty (default).");
}
