// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using Zxv.Interop;
using Zxv.Ledger;

namespace Zxv.Evm;

/// <summary>One party of a state channel with an explicit balance.</summary>
/// <param name="Address">The party's 20-byte Ethereum address.</param>
/// <param name="Balance">Its signed balance in minor units.</param>
public readonly record struct ChannelParty(ReadOnlyMemory<byte> Address, long Balance);

/// <summary>One party of a state channel whose balance is read from a ledger account.</summary>
/// <param name="Address">The party's 20-byte Ethereum address.</param>
/// <param name="Account">The ledger account whose EQUITY (888) balance is the party's balance.</param>
public readonly record struct LedgerChannelParty(ReadOnlyMemory<byte> Address, AccountId Account);

/// <summary>An encoded state-channel state.</summary>
/// <param name="Encoding"><c>abi.encode(bytes32 channelId, uint256 nonce, address[] parties, int256[] balances)</c>.</param>
/// <param name="Digest"><c>keccak256(Encoding)</c>: what each party signs (EIP-191) in its own wallet.</param>
public sealed record EvmState(ReadOnlyMemory<byte> Encoding, ReadOnlyMemory<byte> Digest);

/// <summary>
/// Encodes the ledger's exact integer balances for a Solidity state-channel
/// contract (<c>sdk/solidity/ZxvStateChannel.sol</c>). Needs
/// <see cref="CapabilityRights.Evm"/> (and <see cref="CapabilityRights.LedgerRead"/>
/// for <see cref="LedgerState"/>). ZXV has no secp256k1: parties sign the
/// digest with their own Ethereum keys. Thread-safe.
/// </summary>
public sealed class StateChannel
{
    /// <summary>Length of an Ethereum address.</summary>
    public const int AddressBytes = 20;

    /// <summary>Length of a channel id.</summary>
    public const int ChannelIdBytes = 32;

    private readonly Capability _cap;

    /// <summary>Initializes an encoder that presents <paramref name="capability"/> on every call.</summary>
    /// <param name="capability">The capability.</param>
    public StateChannel(Capability capability)
    {
        ArgumentNullException.ThrowIfNull(capability);
        _cap = capability;
    }

    /// <summary>Encodes a state with explicit balances.</summary>
    /// <param name="channelId">32-byte channel id.</param>
    /// <param name="nonce">The state's nonce.</param>
    /// <param name="parties">The parties, in contract order.</param>
    /// <returns>The encoding and its digest.</returns>
    public unsafe EvmState EncodeState(ReadOnlySpan<byte> channelId, ulong nonce, IReadOnlyList<ChannelParty> parties)
    {
        ArgumentNullException.ThrowIfNull(parties);
        CryptoGuard.Length(channelId, ChannelIdBytes, nameof(channelId));
        byte[] addresses = PackAddresses(parties.Count, i => parties[i].Address, nameof(parties));
        var balances = new long[parties.Count];
        for (int i = 0; i < balances.Length; i++)
        {
            balances[i] = parties[i].Balance;
        }

        _cap.Enter("StateChannel.EncodeState");
        fixed (byte* id = channelId)
        fixed (byte* a = addresses)
        fixed (long* b = balances)
        {
            byte* idp = id, ap = a;
            long* bp = b;
            uint n = (uint)balances.Length;
            return Encode((byte* buf, nuint cap, out nuint len, byte* digest) =>
                NativeMethods.EvmEncodeState(_cap.Handle, idp, nonce, ap, bp, n, buf, cap, out len, digest), "StateChannel.EncodeState");
        }
    }

    /// <summary>Encodes a state reading each party's EQUITY (888) balance from the ledger (all accounts in <paramref name="currency"/>).</summary>
    /// <param name="channelId">32-byte channel id.</param>
    /// <param name="nonce">The state's nonce.</param>
    /// <param name="currency">The accounts' currency.</param>
    /// <param name="parties">The parties, in contract order.</param>
    /// <returns>The encoding and its digest.</returns>
    public unsafe EvmState LedgerState(ReadOnlySpan<byte> channelId, ulong nonce, string currency, IReadOnlyList<LedgerChannelParty> parties)
    {
        ArgumentNullException.ThrowIfNull(parties);
        Guard.Text(currency, nameof(currency));
        CryptoGuard.Length(channelId, ChannelIdBytes, nameof(channelId));
        byte[] addresses = PackAddresses(parties.Count, i => parties[i].Address, nameof(parties));
        var accounts = new uint[parties.Count];
        for (int i = 0; i < accounts.Length; i++)
        {
            accounts[i] = parties[i].Account.Value;
        }

        _cap.Enter("StateChannel.LedgerState");
        fixed (byte* id = channelId)
        fixed (byte* a = addresses)
        fixed (uint* acc = accounts)
        {
            byte* idp = id, ap = a;
            uint* accp = acc;
            uint n = (uint)accounts.Length;
            return Encode((byte* buf, nuint cap, out nuint len, byte* digest) =>
                NativeMethods.EvmLedgerState(_cap.Handle, idp, nonce, currency, accp, ap, n, buf, cap, out len, digest), "StateChannel.LedgerState");
        }
    }

    private static byte[] PackAddresses(int count, Func<int, ReadOnlyMemory<byte>> address, string paramName)
    {
        if (count == 0)
        {
            throw new ArgumentException("A state channel needs at least one party.", paramName);
        }

        var packed = new byte[count * AddressBytes];
        for (int i = 0; i < count; i++)
        {
            ReadOnlySpan<byte> a = address(i).Span;
            if (a.Length != AddressBytes)
            {
                throw new ArgumentException($"Party {i}: an address is {AddressBytes} bytes, got {a.Length}.", paramName);
            }

            a.CopyTo(packed.AsSpan(i * AddressBytes));
        }

        return packed;
    }

    private unsafe EvmState Encode(EncodeCall call, string operation)
    {
        var digest = new byte[32];
        fixed (byte* d = digest)
        {
            int rc = call(null, 0, out nuint needed, d);
            if (rc != (int)ZxvStatus.BufferTooSmall)
            {
                _cap.Check(rc, operation);
            }

            var encoding = new byte[checked((int)needed + 1)];
            fixed (byte* e = encoding)
            {
                _cap.Check(call(e, (nuint)encoding.Length, out nuint len, d), operation);
                return new EvmState(encoding.AsMemory(0, checked((int)len)), digest);
            }
        }
    }

    private unsafe delegate int EncodeCall(byte* buffer, nuint capacity, out nuint length, byte* digest);
}
