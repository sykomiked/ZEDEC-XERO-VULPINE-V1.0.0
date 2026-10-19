// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Runtime.InteropServices;

namespace Zxv.Interop;

/// <summary>Owns a native <c>zxv_ctx*</c>. Released with <c>zxv_ctx_destroy</c>.</summary>
/// <remarks>
/// The marshaller adds a reference for the duration of every native call, so
/// disposing the handle on one thread while another thread is inside a call is
/// safe: the native object is destroyed after the last call returns.
/// </remarks>
public sealed class ZxvContextHandle : SafeHandle
{
    /// <summary>Initializes an invalid handle; the marshaller fills it in.</summary>
    public ZxvContextHandle()
        : base(IntPtr.Zero, ownsHandle: true)
    {
    }

    /// <inheritdoc/>
    public override bool IsInvalid => handle == IntPtr.Zero;

    /// <inheritdoc/>
    protected override bool ReleaseHandle()
    {
        NativeMethods.CtxDestroy(handle);
        return true;
    }
}

/// <summary>Owns a native <c>zxv_msg*</c> (one ISO 20022 message being built).</summary>
public sealed class ZxvMessageHandle : SafeHandle
{
    /// <summary>Initializes an invalid handle; the marshaller fills it in.</summary>
    public ZxvMessageHandle()
        : base(IntPtr.Zero, ownsHandle: true)
    {
    }

    /// <inheritdoc/>
    public override bool IsInvalid => handle == IntPtr.Zero;

    /// <inheritdoc/>
    protected override bool ReleaseHandle()
    {
        NativeMethods.MsgDestroy(handle);
        return true;
    }
}

/// <summary>Owns a native <c>zxv_netting*</c> (one netting cycle).</summary>
public sealed class ZxvNettingHandle : SafeHandle
{
    /// <summary>Initializes an invalid handle; the marshaller fills it in.</summary>
    public ZxvNettingHandle()
        : base(IntPtr.Zero, ownsHandle: true)
    {
    }

    /// <inheritdoc/>
    public override bool IsInvalid => handle == IntPtr.Zero;

    /// <inheritdoc/>
    protected override bool ReleaseHandle()
    {
        NativeMethods.NettingDestroy(handle);
        return true;
    }
}

/// <summary>
/// Base of the ABI 1.1 handles. Besides owning one native object, it keeps a
/// reference on every handle the native object depends on (its context, the
/// parent capability, the model of a session, the budget bound to a session),
/// so the native rule "a child is destroyed before its parent" (R6) holds no
/// matter in which order managed code disposes or finalizes them.
/// </summary>
public abstract class ZxvDependentHandle : SafeHandle
{
    private readonly object _gate = new();
    private SafeHandle[] _dependencies = Array.Empty<SafeHandle>();

    /// <summary>Initializes an invalid handle; the marshaller fills it in.</summary>
    protected ZxvDependentHandle()
        : base(IntPtr.Zero, ownsHandle: true)
    {
    }

    /// <inheritdoc/>
    public override bool IsInvalid => handle == IntPtr.Zero;

    /// <summary>Keeps <paramref name="dependency"/> alive until this handle is released.</summary>
    /// <param name="dependency">A handle the native object refers to.</param>
    internal void AddDependency(SafeHandle dependency)
    {
        bool added = false;
        dependency.DangerousAddRef(ref added);
        lock (_gate)
        {
            var next = new SafeHandle[_dependencies.Length + 1];
            _dependencies.CopyTo(next, 0);
            next[^1] = dependency;
            _dependencies = next;
        }
    }

    /// <summary>Drops one reference taken by <see cref="AddDependency"/>.</summary>
    /// <param name="dependency">The handle to let go of.</param>
    internal void RemoveDependency(SafeHandle dependency)
    {
        lock (_gate)
        {
            int i = Array.IndexOf(_dependencies, dependency);
            if (i < 0)
            {
                return;
            }

            var next = new SafeHandle[_dependencies.Length - 1];
            Array.Copy(_dependencies, 0, next, 0, i);
            Array.Copy(_dependencies, i + 1, next, i, _dependencies.Length - i - 1);
            _dependencies = next;
        }

        dependency.DangerousRelease();
    }

    /// <summary>Destroys the native object.</summary>
    /// <param name="native">The native pointer.</param>
    private protected abstract void Destroy(IntPtr native);

    /// <inheritdoc/>
    protected sealed override bool ReleaseHandle()
    {
        Destroy(handle);
        SafeHandle[] deps;
        lock (_gate)
        {
            deps = _dependencies;
            _dependencies = Array.Empty<SafeHandle>();
        }

        // Release in reverse order of acquisition.
        for (int i = deps.Length - 1; i >= 0; i--)
        {
            deps[i].DangerousRelease();
        }

        return true;
    }
}

/// <summary>Owns a native <c>zxv_cap*</c> (a capability). Released with <c>zxv_cap_destroy</c>.</summary>
public sealed class ZxvCapabilityHandle : ZxvDependentHandle
{
    private protected override void Destroy(IntPtr native) => NativeMethods.CapDestroy(native);
}

/// <summary>Owns a native <c>zxv_lpres*</c> (an attestation registry).</summary>
public sealed class ZxvAttestationsHandle : ZxvDependentHandle
{
    private protected override void Destroy(IntPtr native) => NativeMethods.LpresDestroy(native);
}

/// <summary>Owns a native <c>zxv_hexlog*</c> (a quarantine log, ABI 1.3).</summary>
public sealed class ZxvQuarantineLogHandle : ZxvDependentHandle
{
    private protected override void Destroy(IntPtr native) => NativeMethods.HexlogDestroy(native);
}

/// <summary>Owns a native <c>zxv_hw_profile*</c> (a ZHP-1 machine profile, ABI 1.4). Profiles depend on nothing.</summary>
public sealed class ZxvHwProfileHandle : ZxvDependentHandle
{
    private protected override void Destroy(IntPtr native) => NativeMethods.HwProfileDestroy(native);
}

/// <summary>
/// Base of the ABI 1.4 handles whose native object calls back into managed
/// code: it owns the <see cref="GCHandle"/>s passed as callback <c>user</c>
/// pointers and frees them only after the native object is destroyed, so no
/// callback can reach a freed handle.
/// </summary>
public abstract class ZxvCallbackHandle : ZxvDependentHandle
{
    private readonly object _pinGate = new();
    private GCHandle[] _pins = Array.Empty<GCHandle>();

    /// <summary>Allocates a <see cref="GCHandle"/> to <paramref name="target"/> that lives as long as this handle.</summary>
    /// <param name="target">The managed callback state.</param>
    /// <returns>The value to pass as the callback's <c>user</c> pointer.</returns>
    /// <param name="type">Normal (the handle keeps the target alive) or Weak.</param>
    internal IntPtr Pin(object target, GCHandleType type = GCHandleType.Normal)
    {
        GCHandle h = GCHandle.Alloc(target, type);
        Adopt(h);
        return GCHandle.ToIntPtr(h);
    }

    /// <summary>Takes ownership of <paramref name="handle"/>: it is freed after the native object is destroyed.</summary>
    /// <param name="handle">A handle already passed to native code as a <c>user</c> pointer.</param>
    internal void Adopt(GCHandle handle)
    {
        lock (_pinGate)
        {
            var next = new GCHandle[_pins.Length + 1];
            _pins.CopyTo(next, 0);
            next[^1] = handle;
            _pins = next;
        }
    }

    /// <summary>Destroys the native object with <paramref name="destroy"/>, then frees every pinned callback state.</summary>
    /// <param name="native">The native pointer.</param>
    /// <param name="destroy">The native destroy function.</param>
    private protected void DestroyThenUnpin(IntPtr native, Action<IntPtr> destroy)
    {
        destroy(native);
        GCHandle[] pins;
        lock (_pinGate)
        {
            pins = _pins;
            _pins = Array.Empty<GCHandle>();
        }

        foreach (GCHandle h in pins)
        {
            h.Free();
        }
    }
}

/// <summary>Owns a native <c>zxv_hw_matrix*</c> (an emulation matrix, ABI 1.4) and its implementation callbacks.</summary>
public sealed class ZxvHwMatrixHandle : ZxvCallbackHandle
{
    private protected override void Destroy(IntPtr native) => DestroyThenUnpin(native, NativeMethods.HwMatrixDestroy);
}

/// <summary>Owns a native <c>zxv_hw_region*</c> (a reconfigurable region, ABI 1.4) and its device and event callbacks.</summary>
public sealed class ZxvHwRegionHandle : ZxvCallbackHandle
{
    private protected override void Destroy(IntPtr native) => DestroyThenUnpin(native, NativeMethods.HwRegionDestroy);
}

/// <summary>Owns a native <c>zxv_rmag*</c> (a set of rational resource budgets).</summary>
public sealed class ZxvBudgetsHandle : ZxvDependentHandle
{
    private protected override void Destroy(IntPtr native) => NativeMethods.RmagDestroy(native);
}

/// <summary>Owns a native <c>zxv_phase*</c> (a phase coordinator clock).</summary>
public sealed class ZxvPhaseHandle : ZxvDependentHandle
{
    private protected override void Destroy(IntPtr native) => NativeMethods.PhaseDestroy(native);
}

/// <summary>Owns a native <c>zxv_swarm*</c> (a swarm token economy).</summary>
public sealed class ZxvSwarmHandle : ZxvDependentHandle
{
    private protected override void Destroy(IntPtr native) => NativeMethods.SwarmDestroy(native);
}

/// <summary>Owns a native <c>zxv_model*</c> (a loaded GGUF model).</summary>
public sealed class ZxvModelHandle : ZxvDependentHandle
{
    private protected override void Destroy(IntPtr native) => NativeMethods.ModelDestroy(native);
}

/// <summary>Owns a native <c>zxv_session*</c> (a KV cache over a model).</summary>
public sealed class ZxvSessionHandle : ZxvDependentHandle
{
    private protected override void Destroy(IntPtr native) => NativeMethods.SessionDestroy(native);
}

/// <summary>Owns a native <c>zxv_lora*</c> (a LoRA adapter; not built yet).</summary>
public sealed class ZxvLoraHandle : ZxvDependentHandle
{
    private protected override void Destroy(IntPtr native) => NativeMethods.LoraDestroy(native);
}

/// <summary>Owns a native <c>zxv_treaty*</c> (a treaty, ABI 1.5; holds no context).</summary>
public sealed class ZxvTreatyHandle : ZxvDependentHandle
{
    private protected override void Destroy(IntPtr native) => NativeMethods.TreatyDestroy(native);
}

/// <summary>Owns a native <c>zxv_treaty_result*</c> (what running a treaty returned, ABI 1.5; holds no context).</summary>
public sealed class ZxvTreatyResultHandle : ZxvDependentHandle
{
    private protected override void Destroy(IntPtr native) => NativeMethods.TreatyResultDestroy(native);
}

/// <summary>Owns a native <c>zxv_aipi*</c> (an offline AIPI node, ABI 1.5; destroy wipes its node key).</summary>
public sealed class ZxvAipiHandle : ZxvDependentHandle
{
    private protected override void Destroy(IntPtr native) => NativeMethods.AipiDestroy(native);
}

/// <summary>Owns a native <c>zxv_token_wallet*</c> (a token wallet, ABI 1.5).</summary>
public sealed class ZxvTokenWalletHandle : ZxvDependentHandle
{
    private protected override void Destroy(IntPtr native) => NativeMethods.TokenWalletDestroy(native);
}

/// <summary>Owns a native <c>zxv_vhub*</c> (a V-Bill demo hub and its ledger, ABI 1.5).</summary>
public sealed class ZxvVHubHandle : ZxvDependentHandle
{
    private protected override void Destroy(IntPtr native) => NativeMethods.VhubDestroy(native);
}

/// <summary>Owns a native <c>zxv_freight_join*</c> (a freight set being reassembled, ABI 1.5; holds no context).</summary>
public sealed class ZxvFreightJoinHandle : ZxvDependentHandle
{
    private protected override void Destroy(IntPtr native) => NativeMethods.FreightJoinDestroy(native);
}

/// <summary>Owns a native <c>zxv_freight_ops_writer*</c> (an op stream being built, ABI 1.5; holds no context).</summary>
public sealed class ZxvFreightOpsWriterHandle : ZxvDependentHandle
{
    private protected override void Destroy(IntPtr native) => NativeMethods.FreightOpsWriterDestroy(native);
}

/// <summary>Owns a native <c>zxv_w4_agent*</c> (a Web 4 key handle, ABI 1.5; destroy wipes the private key).</summary>
public sealed class ZxvW4AgentHandle : ZxvDependentHandle
{
    private protected override void Destroy(IntPtr native) => NativeMethods.W4AgentDestroy(native);
}

/// <summary>Owns a native <c>zxv_w4_dir*</c> (a directory of verified agent cards, ABI 1.5; holds no context).</summary>
public sealed class ZxvW4DirHandle : ZxvDependentHandle
{
    private protected override void Destroy(IntPtr native) => NativeMethods.W4DirDestroy(native);
}

/// <summary>Owns a native <c>zxv_w4_manifest*</c> (a tool manifest, ABI 1.5; holds no context).</summary>
public sealed class ZxvW4ManifestHandle : ZxvDependentHandle
{
    private protected override void Destroy(IntPtr native) => NativeMethods.W4ManifestDestroy(native);
}

/// <summary>Owns a native <c>zxv_w4_consent_log*</c> (a single-use consent log, ABI 1.5; holds no context).</summary>
public sealed class ZxvW4ConsentLogHandle : ZxvDependentHandle
{
    private protected override void Destroy(IntPtr native) => NativeMethods.W4ConsentLogDestroy(native);
}

/// <summary>Owns a native <c>zxv_paradox*</c> (a paradox book: logs and registry, ABI 1.5).</summary>
public sealed class ZxvParadoxHandle : ZxvDependentHandle
{
    private protected override void Destroy(IntPtr native) => NativeMethods.ParadoxDestroy(native);
}

/// <summary>Owns a native <c>zxv_paradox_policy*</c> (a bounded constructive policy, ABI 1.5).</summary>
public sealed class ZxvParadoxPolicyHandle : ZxvDependentHandle
{
    private protected override void Destroy(IntPtr native) => NativeMethods.ParadoxPolicyDestroy(native);
}

/// <summary>Owns a native <c>zxv_paradox_hold*</c> (a neutral hold of candidate branches, ABI 1.5).</summary>
public sealed class ZxvParadoxHoldHandle : ZxvDependentHandle
{
    private protected override void Destroy(IntPtr native) => NativeMethods.ParadoxHoldDestroy(native);
}

/// <summary>Owns a native <c>zxv_paradox_graph*</c> (a complex-paradox dependency graph, ABI 1.5).</summary>
public sealed class ZxvParadoxGraphHandle : ZxvDependentHandle
{
    private protected override void Destroy(IntPtr native) => NativeMethods.ParadoxGraphDestroy(native);
}

/// <summary>Owns a native <c>zxv_vino_play*</c> (a play-lane voucher sandbox, ABI 1.6). Destroyed before its context (R6).</summary>
public sealed class ZxvVinoPlayHandle : ZxvDependentHandle
{
    private protected override void Destroy(IntPtr native) => NativeMethods.VinoPlayDestroy(native);
}

/// <summary>Owns a native <c>zxv_vino_meter*</c> (one device's daily spend counter, ABI 1.6). Destroyed before its context (R6).</summary>
public sealed class ZxvVinoMeterHandle : ZxvDependentHandle
{
    private protected override void Destroy(IntPtr native) => NativeMethods.VinoMeterDestroy(native);
}
