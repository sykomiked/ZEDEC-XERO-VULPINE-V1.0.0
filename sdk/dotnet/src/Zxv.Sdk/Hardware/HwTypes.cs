// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Buffers.Binary;
using System.Collections.Generic;

namespace Zxv.Hardware;

/// <summary>How an implementation is built (<c>ZXV_HW_KIND_*</c>), in the order the matrix prefers them at equal cost; also the value of <see cref="HwCapKeys.Kind"/>.</summary>
public enum HwKind
{
    /// <summary>Silicon built for the function.</summary>
    Hard = 0,

    /// <summary>A reconfigurable fabric region.</summary>
    Fabric = 1,

    /// <summary>A microcoded unit or soft core.</summary>
    Micro = 2,

    /// <summary>Another local component standing in.</summary>
    Peer = 3,

    /// <summary>A software emulator.</summary>
    Soft = 4,

    /// <summary>Another machine; never serves key-bearing work.</summary>
    Remote = 5,
}

/// <summary>Placement policy flags (<c>ZXV_HW_POLICY_*</c>).</summary>
[Flags]
public enum HwPolicy
{
    /// <summary>No constraint.</summary>
    None = 0,

    /// <summary>Always local, never memoized; once set it cannot be cleared.</summary>
    KeyBearing = 0x1,

    /// <summary>No REMOTE implementation.</summary>
    LocalOnly = 0x2,
}

/// <summary>Implementation status flags (<c>ZXV_HW_PF_*</c>).</summary>
[Flags]
public enum HwProviderStatus
{
    /// <summary>No flag.</summary>
    None = 0,

    /// <summary>Its resource is faulted or offline.</summary>
    Fault = 0x01,

    /// <summary>A shadow check found it disagreeing.</summary>
    Disputed = 0x02,

    /// <summary>It failed the golden vectors.</summary>
    Failed = 0x04,

    /// <summary>Not proven against the current contract.</summary>
    Unproven = 0x08,

    /// <summary>Disabled.</summary>
    Disabled = 0x10,

    /// <summary>The function's policy excludes it.</summary>
    ExcludedByPolicy = 0x20,

    /// <summary>Serving the function now.</summary>
    Serving = 0x40,
}

/// <summary>Coarse power classes (<c>ZXV_HW_POWER_*</c>).</summary>
public enum HwPowerClass
{
    /// <summary>Not declared.</summary>
    Unknown = 0,

    /// <summary>At most 10 mW.</summary>
    UpTo10Milliwatts = 1,

    /// <summary>At most 100 mW.</summary>
    UpTo100Milliwatts = 2,

    /// <summary>At most 1 W.</summary>
    UpTo1Watt = 3,

    /// <summary>At most 10 W.</summary>
    UpTo10Watts = 4,

    /// <summary>At most 100 W.</summary>
    UpTo100Watts = 5,

    /// <summary>Above 100 W.</summary>
    Above100Watts = 6,
}

/// <summary>Resource states (<c>ZXV_HW_RES_*</c>).</summary>
public enum HwResourceState
{
    /// <summary>Working.</summary>
    Ok = 0,

    /// <summary>Faulted: its implementations stop serving.</summary>
    Faulted = 1,

    /// <summary>Being reconfigured.</summary>
    Reconfiguring = 2,

    /// <summary>Offline.</summary>
    Offline = 3,
}

/// <summary>A recorded step of a hardware update or contract change (<c>ZXV_HW_EV_*</c>).</summary>
public enum HwEvent
{
    /// <summary>Not an event (never reported).</summary>
    None = 0,

    /// <summary>Verified and loaded into the inactive slot.</summary>
    Staged = 1,

    /// <summary>Switched to and proven: probation begins.</summary>
    Tested = 2,

    /// <summary>Committed: permanent, floor raised.</summary>
    Activated = 3,

    /// <summary>Back to the previous image (or to S0).</summary>
    RolledBack = 4,

    /// <summary>Refused before loading (signature, replay).</summary>
    Rejected = 5,

    /// <summary>Failed in hardware; the payload is blocked forever.</summary>
    Quarantined = 6,

    /// <summary>A function's golden vectors replaced (matrix log).</summary>
    Contract = 7,
}

/// <summary>Package kinds (<c>ZXV_HW_IMG_*</c>).</summary>
public enum HwImageKind
{
    /// <summary>Not a package kind.</summary>
    None = 0,

    /// <summary>A full bitstream.</summary>
    Bitstream = 1,

    /// <summary>A partial bitstream.</summary>
    PartialBitstream = 2,

    /// <summary>Microcode.</summary>
    Microcode = 3,

    /// <summary>Firmware.</summary>
    Firmware = 4,

    /// <summary>A new golden-vector contract for a function (<see cref="HwPackageSigner.BuildContract"/>).</summary>
    Vectors = 5,
}

/// <summary>Region states (<c>ZXV_HW_REGION_*</c>).</summary>
public enum HwRegionState
{
    /// <summary>No image: S0.</summary>
    Empty = 0,

    /// <summary>An image is active.</summary>
    Active = 1,

    /// <summary>An image is staged in the inactive slot.</summary>
    Staged = 2,

    /// <summary>The tested image is on probation.</summary>
    Probation = 3,

    /// <summary>Halted: the audit log is full (fail closed).</summary>
    Halted = 4,
}

/// <summary>How a target component is served on a host (<c>ZXV_HW_NATIVE</c>, <c>_EMULATE</c>, <c>_GROUP</c>).</summary>
public enum HwPairKind
{
    /// <summary>A host component satisfies it.</summary>
    Native = 0,

    /// <summary>Nothing does: the matrix emulates it.</summary>
    Emulate = 1,

    /// <summary>A device or package node: nothing to run.</summary>
    Group = 2,
}

/// <summary>One node of a <see cref="HwProfile"/>.</summary>
/// <param name="Id">Its id.</param>
/// <param name="Parent">Its parent's id, or <see cref="HwProfile.Root"/>.</param>
/// <param name="ComponentClass">Its class (<see cref="HwClasses"/>).</param>
/// <param name="CapCount">How many capabilities it carries.</param>
public readonly record struct HwNode(uint Id, uint Parent, uint ComponentClass, uint CapCount);

/// <summary>How a target profile matches a host.</summary>
/// <param name="NativeCount">Target components a host component runs natively.</param>
/// <param name="EmulatedCount">Target components the matrix must emulate.</param>
public readonly record struct HwMatch(uint NativeCount, uint EmulatedCount);

/// <summary>The pairing of one target node with a host.</summary>
/// <param name="TargetId">The target node id.</param>
/// <param name="Kind">How it is served.</param>
/// <param name="HostId">The host node id (for <see cref="HwPairKind.Native"/>).</param>
/// <param name="UnmetKey">The first capability the host lacks (for <see cref="HwPairKind.Emulate"/>; 0 if the host has no component of that class).</param>
public readonly record struct HwPairing(uint TargetId, HwPairKind Kind, uint HostId, uint UnmetKey);

/// <summary>A function's contract and placement policy.</summary>
/// <param name="Contract">The contract version.</param>
/// <param name="Policy">Its placement flags.</param>
/// <param name="Vectors">How many golden vectors the contract holds.</param>
public readonly record struct HwFunctionInfo(uint Contract, HwPolicy Policy, uint Vectors);

/// <summary>What discovery reports for one implementation.</summary>
/// <param name="FunctionId">The function it implements.</param>
/// <param name="Kind">How it is built.</param>
/// <param name="Proof">True serves, False failed a vector, Neither unproven, Both disputed.</param>
/// <param name="Contract">The contract version it last proved.</param>
/// <param name="Flags">Its status flags.</param>
/// <param name="ThroughputClass">0 (slowest) .. 15.</param>
/// <param name="Power">Its declared power class.</param>
/// <param name="Calls">How often it ran.</param>
/// <param name="CostSpent">The cost it accrued.</param>
public readonly record struct HwProviderInfo(uint FunctionId, HwKind Kind, Truth Proof, uint Contract, HwProviderStatus Flags, uint ThroughputClass, HwPowerClass Power, ulong Calls, ulong CostSpent);

/// <summary>Memo counters.</summary>
/// <param name="Hits">Repeated inputs answered from the memo.</param>
/// <param name="Misses">Inputs that ran an implementation.</param>
public readonly record struct HwMemoStats(ulong Hits, ulong Misses);

/// <summary>A region's state.</summary>
/// <param name="State">Its state.</param>
/// <param name="Version">The active image's version.</param>
/// <param name="Floor">The anti-rollback floor.</param>
/// <param name="Quarantined">How many images were refused or quarantined.</param>
public readonly record struct HwRegionInfo(HwRegionState State, uint Version, uint Floor, uint Quarantined);

/// <summary>The result of <see cref="HwMatrix.Shadow"/>.</summary>
/// <param name="Output">The bound implementation's output.</param>
/// <param name="Checked">Whether a second implementation ran and agreed.</param>
public sealed record HwShadowResult(ReadOnlyMemory<byte> Output, bool Checked);

/// <summary>The head of a hardware audit log.</summary>
/// <param name="Head">The last record's hash (32 bytes).</param>
/// <param name="Count">The number of records.</param>
public sealed record HwAuditHead(ReadOnlyMemory<byte> Head, uint Count);

/// <summary>One record of a hardware audit log (a region's or a matrix's): a SHA-256 hash chain, domain <c>zxv/hw-audit/v1</c>.</summary>
/// <param name="Event">What happened.</param>
/// <param name="Reason">The failure status (0 for none).</param>
/// <param name="Id">The region id, or the function id for <see cref="HwEvent.Contract"/>.</param>
/// <param name="Version">The image or contract version.</param>
/// <param name="Tag">The hexagram tag; forbidden for rejections and quarantines.</param>
/// <param name="Subject">The SHA-256 of the package header.</param>
/// <param name="Hash">This record's chain hash (the last one is the head).</param>
public sealed record HwAuditRecord(HwEvent Event, int Reason, uint Id, uint Version, Hexagram Tag, ReadOnlyMemory<byte> Subject, ReadOnlyMemory<byte> Hash);

/// <summary>One step reported by a <see cref="HwRegion"/>.</summary>
public sealed class HwRegionEventArgs : EventArgs
{
    internal HwRegionEventArgs(uint regionId, HwEvent kind, int reason, uint version)
    {
        RegionId = regionId;
        Kind = kind;
        Reason = reason;
        Version = version;
    }

    /// <summary>Gets the region.</summary>
    public uint RegionId { get; }

    /// <summary>Gets what happened.</summary>
    public HwEvent Kind { get; }

    /// <summary>Gets the failure status, 0 for none.</summary>
    public int Reason { get; }

    /// <summary>Gets the image version.</summary>
    public uint Version { get; }
}

/// <summary>
/// One golden vector of an engine contract: an opcode, an input and the
/// exact output every implementation must reproduce (at most
/// <see cref="MaxOutput"/> bytes).
/// </summary>
public sealed class HwVector
{
    /// <summary>The longest expected output.</summary>
    public const int MaxOutput = 512;

    /// <summary>The most vectors in one contract.</summary>
    public const int MaxVectors = 16;

    /// <summary>Initializes a new instance of the <see cref="HwVector"/> class.</summary>
    /// <param name="op">The operation.</param>
    /// <param name="input">The input bytes (copied).</param>
    /// <param name="expect">The expected output (copied; at most <see cref="MaxOutput"/> bytes).</param>
    public HwVector(uint op, ReadOnlySpan<byte> input, ReadOnlySpan<byte> expect)
    {
        if (expect.Length > MaxOutput)
        {
            throw new ArgumentException($"An expected output is at most {MaxOutput} bytes.", nameof(expect));
        }

        Op = op;
        Input = input.ToArray();
        Expect = expect.ToArray();
    }

    /// <summary>Gets the operation.</summary>
    public uint Op { get; }

    /// <summary>Gets the input.</summary>
    public ReadOnlyMemory<byte> Input { get; }

    /// <summary>Gets the expected output.</summary>
    public ReadOnlyMemory<byte> Expect { get; }

    /// <summary>
    /// Encodes the vector list of a <see cref="HwImageKind.Vectors"/> package:
    /// for each vector u32 op, u32 input length, input, u32 output length,
    /// output (little-endian).
    /// </summary>
    /// <param name="vectors">1..<see cref="MaxVectors"/> vectors.</param>
    /// <returns>The payload.</returns>
    public static byte[] EncodeList(IReadOnlyList<HwVector> vectors)
    {
        ArgumentNullException.ThrowIfNull(vectors);
        if (vectors.Count is 0 or > MaxVectors)
        {
            throw new ArgumentException($"A contract has 1..{MaxVectors} vectors.", nameof(vectors));
        }

        int size = 0;
        foreach (HwVector v in vectors)
        {
            size += 12 + v.Input.Length + v.Expect.Length;
        }

        var buf = new byte[size];
        int o = 0;
        foreach (HwVector v in vectors)
        {
            BinaryPrimitives.WriteUInt32LittleEndian(buf.AsSpan(o), v.Op);
            BinaryPrimitives.WriteUInt32LittleEndian(buf.AsSpan(o + 4), (uint)v.Input.Length);
            v.Input.Span.CopyTo(buf.AsSpan(o + 8));
            o += 8 + v.Input.Length;
            BinaryPrimitives.WriteUInt32LittleEndian(buf.AsSpan(o), (uint)v.Expect.Length);
            v.Expect.Span.CopyTo(buf.AsSpan(o + 4));
            o += 4 + v.Expect.Length;
        }

        return buf;
    }
}

/// <summary>
/// An implementation of a matrix function: the device side of a matrix entry.
/// Write the output into <paramref name="output"/> and return its length; a
/// length above <c>output.Length</c> reports that it does not fit
/// (<c>ZXV_E_BUFFER</c>). Throw a <see cref="ZxvException"/> to hand its
/// status to the caller of <see cref="HwMatrix.Call(uint, uint, ReadOnlySpan{byte}, int)"/>.
/// </summary>
/// <param name="op">The operation.</param>
/// <param name="input">The input bytes.</param>
/// <param name="output">Room for the output.</param>
/// <returns>The output length.</returns>
public delegate int HwImplementation(uint op, ReadOnlySpan<byte> input, Span<byte> output);

/// <summary>
/// The device side of a <see cref="HwRegion"/>: three calls, so the update
/// policy knows nothing about any vendor's parts. Signal failure by
/// throwing; a <see cref="ZxvException"/> passes its status to libzxv.
/// </summary>
public interface IHwRegionDevice
{
    /// <summary>Loads <paramref name="payload"/> into <paramref name="slot"/> (0 or 1).</summary>
    /// <param name="slot">The inactive slot.</param>
    /// <param name="payload">The image payload.</param>
    void Load(uint slot, ReadOnlySpan<byte> payload);

    /// <summary>Writes the SHA-256 of what <paramref name="slot"/> really holds.</summary>
    /// <param name="slot">The slot.</param>
    /// <param name="digest">32 bytes to fill.</param>
    void ReadBack(uint slot, Span<byte> digest);

    /// <summary>Makes <paramref name="slot"/> the active one.</summary>
    /// <param name="slot">The slot.</param>
    void Activate(uint slot);
}

/// <summary>
/// ZHP-1 component classes (<c>ZXV_HW_CLASS_*</c>); the dotted names are the
/// profile's text names. Classes from <see cref="Private"/> up are an implementer's own.
/// </summary>
public static class HwClasses
{
    /// <summary><c>ZXV_HW_CLASS_MACHINE</c> (device).</summary>
    public const uint Machine = 1;

    /// <summary><c>ZXV_HW_CLASS_CPU_CLUSTER</c> (cluster).</summary>
    public const uint CpuCluster = 2;

    /// <summary><c>ZXV_HW_CLASS_CPU_CORE</c> (core).</summary>
    public const uint CpuCore = 3;

    /// <summary><c>ZXV_HW_CLASS_CACHE</c> (mem.cache).</summary>
    public const uint Cache = 4;

    /// <summary><c>ZXV_HW_CLASS_MEMORY</c> (mem).</summary>
    public const uint Memory = 5;

    /// <summary><c>ZXV_HW_CLASS_INTERCONNECT</c> (interconnect).</summary>
    public const uint Interconnect = 6;

    /// <summary><c>ZXV_HW_CLASS_MATRIX_UNIT</c> (engine.tensor.int).</summary>
    public const uint MatrixUnit = 7;

    /// <summary><c>ZXV_HW_CLASS_VECTOR_UNIT</c> (engine.vector).</summary>
    public const uint VectorUnit = 8;

    /// <summary><c>ZXV_HW_CLASS_TERNARY_UNIT</c> (engine.ternary).</summary>
    public const uint TernaryUnit = 9;

    /// <summary><c>ZXV_HW_CLASS_HASH_UNIT</c> (engine.hash).</summary>
    public const uint HashUnit = 10;

    /// <summary><c>ZXV_HW_CLASS_CRYPTO_UNIT</c> (engine.crypto).</summary>
    public const uint CryptoUnit = 11;

    /// <summary><c>ZXV_HW_CLASS_CODEC_UNIT</c> (engine.codec).</summary>
    public const uint CodecUnit = 12;

    /// <summary><c>ZXV_HW_CLASS_RECONF_FABRIC</c> (fabric.reconfig).</summary>
    public const uint ReconfFabric = 13;

    /// <summary><c>ZXV_HW_CLASS_STORAGE</c> (io.storage).</summary>
    public const uint Storage = 14;

    /// <summary><c>ZXV_HW_CLASS_NETWORK</c> (io.network).</summary>
    public const uint Network = 15;

    /// <summary><c>ZXV_HW_CLASS_RADIO</c> (radio).</summary>
    public const uint Radio = 16;

    /// <summary><c>ZXV_HW_CLASS_SENSOR</c> (sensor).</summary>
    public const uint Sensor = 17;

    /// <summary><c>ZXV_HW_CLASS_ACTUATOR</c> (actuator).</summary>
    public const uint Actuator = 18;

    /// <summary><c>ZXV_HW_CLASS_CLOCK</c> (clock).</summary>
    public const uint Clock = 19;

    /// <summary><c>ZXV_HW_CLASS_POWER</c> (power).</summary>
    public const uint Power = 20;

    /// <summary><c>ZXV_HW_CLASS_SECURE_ELEMENT</c> (secure.element).</summary>
    public const uint SecureElement = 21;

    /// <summary><c>ZXV_HW_CLASS_DISPLAY</c> (io.display).</summary>
    public const uint Display = 22;

    /// <summary><c>ZXV_HW_CLASS_AUDIO</c> (io.audio).</summary>
    public const uint Audio = 23;

    /// <summary><c>ZXV_HW_CLASS_WIRE</c> (io.wire).</summary>
    public const uint Wire = 24;

    /// <summary><c>ZXV_HW_CLASS_GPU</c> (engine.gpu).</summary>
    public const uint Gpu = 25;

    /// <summary><c>ZXV_HW_CLASS_PACKAGE</c> (package).</summary>
    public const uint Package = 26;

    /// <summary><c>ZXV_HW_CLASS_LANE</c> (lane).</summary>
    public const uint Lane = 27;

    /// <summary><c>ZXV_HW_CLASS_LOGIC_UNIT</c> (engine.logic64).</summary>
    public const uint LogicUnit = 28;

    /// <summary><c>ZXV_HW_CLASS_NTT_UNIT</c> (engine.ntt).</summary>
    public const uint NttUnit = 29;

    /// <summary><c>ZXV_HW_CLASS_FRAME_UNIT</c> (engine.frame.ubh168).</summary>
    public const uint FrameUnit = 30;

    /// <summary><c>ZXV_HW_CLASS_GCD_UNIT</c> (engine.gcd).</summary>
    public const uint GcdUnit = 31;

    /// <summary><c>ZXV_HW_CLASS_MEM_CHANNEL</c> (mem.channel).</summary>
    public const uint MemChannel = 32;

    /// <summary><c>ZXV_HW_CLASS_SECURE_ISLAND</c> (secure.island).</summary>
    public const uint SecureIsland = 33;

    /// <summary><c>ZXV_HW_CLASS_IO</c> (io).</summary>
    public const uint Io = 34;

    /// <summary><c>ZXV_HW_CLASS_PRIVATE</c>.</summary>
    public const uint Private = 0x8000_0000;
}

/// <summary>
/// ZHP-1 capability keys (<c>ZXV_HW_CAP_*</c>) and the <see cref="Isa"/> values.
/// Bit 15 marks a critical key, bits 14-13 the comparison (MIN, EXACT, MASK,
/// INFO), bits 12-0 the id; ids 0x1000-0x1FFF are private. The values never change.
/// </summary>
public static class HwCapKeys
{
    /// <summary><c>ZXV_HW_CAP_WORD_BITS</c>.</summary>
    public const uint WordBits = 0x0001;

    /// <summary><c>ZXV_HW_CAP_COUNT</c>.</summary>
    public const uint Count = 0x0002;

    /// <summary><c>ZXV_HW_CAP_FREQ_KHZ</c>.</summary>
    public const uint FreqKhz = 0x6003;

    /// <summary><c>ZXV_HW_CAP_BYTES</c>.</summary>
    public const uint Bytes = 0x0004;

    /// <summary><c>ZXV_HW_CAP_LINE_BYTES</c>.</summary>
    public const uint LineBytes = 0x6005;

    /// <summary><c>ZXV_HW_CAP_ISA</c>.</summary>
    public const uint Isa = 0x2006;

    /// <summary><c>ZXV_HW_CAP_INT_WIDTHS</c>.</summary>
    public const uint IntWidths = 0x4007;

    /// <summary><c>ZXV_HW_CAP_RADIX</c>.</summary>
    public const uint Radix = 0x2008;

    /// <summary><c>ZXV_HW_CAP_MACS_PER_CYCLE</c>.</summary>
    public const uint MacsPerCycle = 0x0009;

    /// <summary><c>ZXV_HW_CAP_SIMD</c>.</summary>
    public const uint Simd = 0x400A;

    /// <summary><c>ZXV_HW_CAP_TICK_NS</c>.</summary>
    public const uint TickNs = 0x600B;

    /// <summary><c>ZXV_HW_CAP_REGIONS</c>.</summary>
    public const uint Regions = 0x000C;

    /// <summary><c>ZXV_HW_CAP_BANDWIDTH_MBPS</c>.</summary>
    public const uint BandwidthMbps = 0x000D;

    /// <summary><c>ZXV_HW_CAP_LATENCY_NS</c>.</summary>
    public const uint LatencyNs = 0x600E;

    /// <summary><c>ZXV_HW_CAP_PQ_ALGS</c>.</summary>
    public const uint PqAlgs = 0x400F;

    /// <summary><c>ZXV_HW_CAP_HASH_ALGS</c>.</summary>
    public const uint HashAlgs = 0x4010;

    /// <summary><c>ZXV_HW_CAP_WIRE_BITS</c>.</summary>
    public const uint WireBits = 0x2011;

    /// <summary><c>ZXV_HW_CAP_PEAK_OPS_MHZ</c>.</summary>
    public const uint PeakOpsMhz = 0x6012;

    /// <summary><c>ZXV_HW_CAP_CONTRACT_ID</c>: the engine contract (ZXV_HW_FN_*).</summary>
    public const uint ContractId = 0x2013;

    /// <summary><c>ZXV_HW_CAP_CONTRACT_VER</c>: its golden-vector version (MIN).</summary>
    public const uint ContractVer = 0x0014;

    /// <summary><c>ZXV_HW_CAP_KIND</c>: ZXV_HW_KIND_* serving the node.</summary>
    public const uint Kind = 0x6015;

    /// <summary><c>ZXV_HW_CAP_CAPACITY_CLASS</c>: coarse, 0..15.</summary>
    public const uint CapacityClass = 0x0016;

    /// <summary><c>ZXV_HW_CAP_PERF_BUCKET</c>: coarse, 0..15.</summary>
    public const uint PerfBucket = 0x0017;

    /// <summary><c>ZXV_HW_CAP_POWER_BUCKET</c>: ZXV_HW_POWER_* scale.</summary>
    public const uint PowerBucket = 0x6018;

    /// <summary><c>ZXV_HW_ISA_X86_64</c>: a value of <see cref="Isa"/>.</summary>
    public const ulong IsaX64 = 1;

    /// <summary><c>ZXV_HW_ISA_ARM64</c>: a value of <see cref="Isa"/>.</summary>
    public const ulong IsaArm64 = 2;

    /// <summary><c>ZXV_HW_ISA_RISCV64</c>: a value of <see cref="Isa"/>.</summary>
    public const ulong IsaRiscv64 = 3;

    /// <summary><c>ZXV_HW_ISA_WASM32</c>: a value of <see cref="Isa"/>.</summary>
    public const ulong IsaWasm32 = 4;
}

/// <summary>
/// The standard engines (<c>ZXV_HW_FN_*</c>): contracts every <see cref="HwMatrix"/>
/// starts with, each with a proven SOFT implementation and version-1 golden
/// vectors, so every call works on any device. Integers are little-endian;
/// formats are in <c>sdk/docs/HW_EMULATION.md</c>.
/// </summary>
public static class HwFunctions
{
    /// <summary><c>ZXV_HW_FN_SHA256</c>: op 0: SHA-256.</summary>
    public const uint Sha256 = 0x5A450001;

    /// <summary><c>ZXV_HW_FN_KECCAK</c>: op 0/1 SHA3-256/512, op 2/3 SHAKE.</summary>
    public const uint Keccak = 0x5A450002;

    /// <summary><c>ZXV_HW_FN_MAC</c>: op 0: HMAC-SHA256 (key-bearing).</summary>
    public const uint Mac = 0x5A450003;

    /// <summary><c>ZXV_HW_FN_GCD</c>: op 0: n pairs of u64 -&gt; n gcds.</summary>
    public const uint Gcd = 0x5A450004;

    /// <summary><c>ZXV_HW_FN_LOGIC64</c>: 64-lane four-valued AND / OR / NOT.</summary>
    public const uint Logic64 = 0x5A450005;

    /// <summary><c>ZXV_HW_FN_TENSOR</c>: op 0: one Q8_0 row dot (zt_dot_raw).</summary>
    public const uint Tensor = 0x5A450006;
}
