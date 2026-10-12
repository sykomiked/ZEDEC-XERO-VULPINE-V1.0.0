// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using Zxv.Interop;

namespace Zxv;

/// <content>Factories for the ABI 1.5 objects that belong to a platform.</content>
public sealed partial class ZxvPlatform
{
    /// <summary>Creates an offline AIPI node: an in-memory, content-addressed block store (ABI 1.5, <c>zxv_aipi</c>).</summary>
    /// <param name="capability">A capability of this platform with <see cref="CapabilityRights.Aipi"/>; storing goes through it.</param>
    /// <param name="storeBytes">Store size, <see cref="Aipi.AipiNode.MinStoreBytes"/>..<see cref="Aipi.AipiNode.MaxStoreBytes"/>.</param>
    /// <param name="maxBlocks">Most blocks indexed, 1..<see cref="Aipi.AipiNode.MaxBlocks"/>.</param>
    /// <param name="nodeKey">A 32-byte key that seals private blocks, or empty for none (then private puts are refused).</param>
    /// <returns>The node; dispose it when done.</returns>
    /// <exception cref="CapabilityDeniedException">The capability lacks <see cref="CapabilityRights.Aipi"/>.</exception>
    public Aipi.AipiNode CreateAipiNode(Capability capability, long storeBytes, int maxBlocks, ReadOnlySpan<byte> nodeKey = default) =>
        new(RequireOwn(capability), storeBytes, maxBlocks, nodeKey);

    /// <summary>Creates a token wallet: a holding kept canonical at its own octane (ABI 1.5, <c>zxv_token_wallet</c>).</summary>
    /// <param name="capability">A capability of this platform with <see cref="CapabilityRights.Mint"/>; every change goes through it.</param>
    /// <param name="modelId">A free label, e.g. the swarm model that holds it.</param>
    /// <param name="octane">0..<see cref="Coins.Octane.Max"/>.</param>
    /// <returns>The empty wallet; dispose it when done.</returns>
    /// <exception cref="CapabilityDeniedException">The capability lacks <see cref="CapabilityRights.Mint"/>.</exception>
    public Coins.TokenWallet CreateTokenWallet(Capability capability, uint modelId, int octane = Coins.Octane.Plain) =>
        new(RequireOwn(capability), modelId, octane);

    /// <summary>Creates a demonstration V-Bill hub with its own in-memory ledger (ABI 1.5, <c>zxv_vhub</c>).</summary>
    /// <param name="capability">A capability of this platform with <see cref="CapabilityRights.LedgerPost"/> (and <see cref="CapabilityRights.LedgerRead"/> for reads).</param>
    /// <param name="seed">32 bytes from which every posting's keys derive (deterministic), or empty for a fresh random seed.</param>
    /// <returns>The hub; dispose it when done.</returns>
    public VHub.VBillHub CreateVBillHub(Capability capability, ReadOnlySpan<byte> seed = default) =>
        new(RequireOwn(capability), seed);

    /// <summary>
    /// Creates a Web 4 key handle for an agent or a human (ABI 1.5,
    /// <c>zxv_w4_agent</c>). The private key is generated inside the handle,
    /// never leaves it, and is wiped when it is disposed.
    /// </summary>
    /// <param name="capability">A capability of this platform with <see cref="CapabilityRights.CryptoSign"/> (and <see cref="CapabilityRights.Attest"/> to issue consent).</param>
    /// <param name="name">1..31 characters of [a-z0-9._-].</param>
    /// <param name="nowMs">The binding time (Unix milliseconds).</param>
    /// <param name="seed">32 bytes for a reproducible key, or empty for a fresh random one.</param>
    /// <param name="node">The node hosting this agent (it signs the binding; same platform), or <see langword="null"/> for a self-bound agent.</param>
    /// <returns>The handle; dispose it when done.</returns>
    public Web4.Web4Agent CreateWeb4Agent(Capability capability, string name, ulong nowMs, ReadOnlySpan<byte> seed = default, Web4.Web4Agent? node = null) =>
        new(RequireOwn(capability), name, nowMs, seed, node);

    /// <summary>Creates a paradox book: three hash-chained logs and a signature registry (ABI 1.5, <c>zxv_paradox</c>).</summary>
    /// <param name="capability">A capability of this platform with <see cref="CapabilityRights.Paradox"/>; every write goes through it.</param>
    /// <param name="logCapacity">Records per log, 1..<see cref="Paradox.ParadoxBook.MaxLogRecords"/>.</param>
    /// <param name="registryCapacity">Registry entries, 1..<see cref="Paradox.ParadoxBook.MaxRegistryEntries"/>.</param>
    /// <returns>The book; dispose it when done.</returns>
    /// <exception cref="CapabilityDeniedException">The capability lacks <see cref="CapabilityRights.Paradox"/>.</exception>
    public Paradox.ParadoxBook CreateParadoxBook(Capability capability, int logCapacity, int registryCapacity) =>
        new(RequireOwn(capability), logCapacity, registryCapacity);

    /// <summary>Creates a constructive policy with published bounds (ABI 1.5, <c>zxv_paradox_policy</c>).</summary>
    /// <param name="capability">A capability of this platform with <see cref="CapabilityRights.Paradox"/>.</param>
    /// <param name="lower">Lower bounds (1..64 parameters).</param>
    /// <param name="upper">Upper bounds, each at least its lower bound.</param>
    /// <param name="initial">Initial values, inside their bounds.</param>
    /// <param name="concerns">Money or device flags: then every update goes to the owner.</param>
    /// <returns>The policy; dispose it when done.</returns>
    public Paradox.ParadoxPolicy CreateParadoxPolicy(Capability capability, ReadOnlySpan<long> lower, ReadOnlySpan<long> upper, ReadOnlySpan<long> initial, Paradox.ParadoxConcerns concerns = Paradox.ParadoxConcerns.None) =>
        new(RequireOwn(capability), lower, upper, initial, concerns);

    /// <summary>Creates a neutral hold of candidate branches (ABI 1.5, <c>zxv_paradox_hold</c>).</summary>
    /// <param name="capability">A capability of this platform with <see cref="CapabilityRights.Paradox"/>.</param>
    /// <param name="capacity">Branches, 1..<see cref="Paradox.ParadoxHold.MaxBranches"/>.</param>
    /// <param name="decisive">The net evidence lead that settles a branch (0 means 1).</param>
    /// <returns>The hold; dispose it when done.</returns>
    public Paradox.ParadoxHold CreateParadoxHold(Capability capability, int capacity, int decisive) =>
        new(RequireOwn(capability), capacity, decisive);

    /// <summary>Creates a complex-paradox graph (ABI 1.5, <c>zxv_paradox_graph</c>).</summary>
    /// <param name="capability">A capability of this platform with <see cref="CapabilityRights.Paradox"/>.</param>
    /// <param name="stepLimit">Passes (at least 1) before it escalates to the owner.</param>
    /// <returns>The graph; dispose it when done.</returns>
    public Paradox.ParadoxGraph CreateParadoxGraph(Capability capability, int stepLimit) =>
        new(RequireOwn(capability), stepLimit);

    /// <summary>Gets the ABI 1.5 feature groups the loaded libzxv was built with.</summary>
    public static ZxvFeatures2 Features2 => ZxvLibrary.Features2;
}
