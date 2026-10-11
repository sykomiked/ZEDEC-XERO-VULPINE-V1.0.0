// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Runtime.InteropServices;

namespace Zedec.Zxv
{
    /// <summary>
    /// One deterministic lockstep simulation (emu-state, over libzxv). Not a
    /// MonoBehaviour: drive it from your own Update/FixedUpdate, a server loop
    /// or a test. Not thread-safe: use it from one thread at a time.
    /// </summary>
    /// <remarks>
    /// <para>
    /// State lives in slots: exact <see cref="Rational"/> values and
    /// <see cref="Hexagram"/> tags. A tick runs your <see cref="Step"/>
    /// between <c>begin</c> and <c>commit</c>; it reads each player's input
    /// with <see cref="Input(int)"/> and changes slots. Every committed tick is
    /// hashed (SHA3-256 of the canonical encoding), so peers compare hashes
    /// (<see cref="Compare"/>) to detect a desync.
    /// </para>
    /// <para>
    /// Lockstep only works when every peer runs the same build, the same step
    /// code and the same inputs. This class detects a divergence; it does not
    /// prevent cheating, and it hides nothing from a modified client.
    /// </para>
    /// </remarks>
    public sealed class ZxvLockstep : IDisposable
    {
        /// <summary>The rules for one tick. Return <see cref="ZxvStatus.Ok"/> to commit;
        /// anything else aborts the tick and is returned by <see cref="Advance"/>.</summary>
        public delegate ZxvStatus Step(ZxvLockstep sim, ulong tick);

        private readonly EmuHandle _h;

        public ZxvLockstep(int players, int ratSlots, int hexSlots, int inputBytes, int history, bool predict)
        {
            Native.CheckVersion();
            if (players < 0 || ratSlots < 0 || hexSlots < 0 || inputBytes < 0 || history < 0)
                throw new ZxvException(-1, "arguments must not be negative");
            int rc = Native.zxv_emu_create((uint)players, (uint)ratSlots, (uint)hexSlots, (uint)inputBytes,
                (uint)history, predict ? 1u : 0u, out IntPtr sim);
            ZxvException.ThrowEmu(rc);
            _h = new EmuHandle(sim);
            Players = players;
            InputBytes = inputBytes;
        }

        public int Players { get; }
        public int InputBytes { get; }

        private IntPtr Sim
        {
            get
            {
                if (_h.IsClosed) throw new ObjectDisposedException(nameof(ZxvLockstep));
                return _h.DangerousGetHandle();
            }
        }

        public void Dispose() => _h.Dispose();

        /// <summary>
        /// An exact per-tick budget (charged with <see cref="Charge"/>). The
        /// sim owns a private libzxv context and a capability narrowed to
        /// BUDGET only; nothing else can use it.
        /// </summary>
        public void SetTickBudget(Rational limit)
        {
            IntPtr sim = Sim;
            _h.EnsureBudgetCap();
            ZxvException.ThrowEmu(Native.zxv_emu_set_tick_budget(sim, _h.BudgetCap, limit.Numerator, limit.Denominator));
        }

        /// <summary>Freeze the initial state as tick 0.</summary>
        public void Start() => ZxvException.ThrowEmu(Native.zxv_emu_start(Sim));

        // ---- the fixed-step clock -------------------------------------------

        public void ClockSetStep(Rational seconds, int maxCatchup) =>
            ZxvException.ThrowEmu(Native.zxv_emu_clock_set_step(Sim, seconds.Numerator, seconds.Denominator,
                (uint)Math.Max(0, maxCatchup)));

        /// <summary>Feed elapsed time as integer microseconds (for example from a
        /// <see cref="System.Diagnostics.Stopwatch"/>), never as Time.deltaTime;
        /// returns the ticks due now.</summary>
        public int ClockFeedMicroseconds(long usec)
        {
            ZxvException.ThrowEmu(Native.zxv_emu_clock_feed(Sim, usec, 1000000, out uint due, out _));
            return (int)due;
        }

        /// <summary>The fraction of a tick accumulated, in [0, 1), for render interpolation.</summary>
        public Rational ClockAlpha()
        {
            ZxvException.ThrowEmu(Native.zxv_emu_clock_alpha(Sim, out long n, out long d));
            return Rational.Of(n, d);
        }

        // ---- inputs ----------------------------------------------------------

        /// <summary>Submit a player's input for a tick. Returns the status: Ok, or
        /// Range (outside the window / too late), Duplicate (a different confirmed
        /// input), State (inside a tick), Arg (bad player or length).</summary>
        public ZxvStatus SubmitInput(int player, ulong tick, byte[] bytes)
        {
            if (bytes == null) throw new ArgumentNullException(nameof(bytes));
            if (player < 0) return ZxvStatus.Arg;
            return (ZxvStatus)Native.zxv_emu_input_submit(Sim, (uint)player, tick, bytes, (UIntPtr)bytes.Length);
        }

        /// <summary>Inside a step: the input a player contributes to this tick.</summary>
        public byte[] Input(int player) => Input(player, out _);

        public byte[] Input(int player, out bool predicted)
        {
            var buf = new byte[InputBytes];
            ZxvException.ThrowEmu(Native.zxv_emu_input_get(Sim, (uint)player, buf, (UIntPtr)buf.Length, out _, out int p));
            predicted = p != 0;
            return buf;
        }

        public bool Ready
        {
            get
            {
                ZxvException.ThrowEmu(Native.zxv_emu_ready(Sim, out int r));
                return r != 0;
            }
        }

        public ulong Tick
        {
            get
            {
                ZxvException.ThrowEmu(Native.zxv_emu_tick(Sim, out ulong t));
                return t;
            }
        }

        // ---- ticks -------------------------------------------------------------

        /// <summary>Begin, run <paramref name="step"/>, then commit (Ok) or abort. Returns
        /// Ok, the step's own status, or State when the tick cannot begin (inputs
        /// missing in strict mode, a rollback pending in predict mode).</summary>
        public ZxvStatus Advance(Step step)
        {
            if (step == null) throw new ArgumentNullException(nameof(step));
            IntPtr sim = Sim;
            int rc = Native.zxv_emu_begin(sim);
            if (rc != 0) return (ZxvStatus)rc;
            ZxvStatus st;
            try
            {
                st = step(this, Tick + 1);
            }
            catch
            {
                Native.zxv_emu_abort(sim);
                throw;
            }
            if (st != ZxvStatus.Ok)
            {
                Native.zxv_emu_abort(sim);
                return st;
            }
            ZxvException.ThrowEmu(Native.zxv_emu_commit(sim));
            return ZxvStatus.Ok;
        }

        // ---- state ---------------------------------------------------------------

        public Rational GetRational(int slot)
        {
            ZxvException.ThrowEmu(Native.zxv_emu_rat_get(Sim, (uint)slot, out long n, out long d));
            return Rational.Of(n, d);
        }

        public void SetRational(int slot, Rational value) =>
            ZxvException.ThrowEmu(Native.zxv_emu_rat_set(Sim, (uint)slot, value.Numerator, value.Denominator));

        /// <summary>slot += value, exactly; Range on overflow (slot unchanged).</summary>
        public ZxvStatus AddRational(int slot, Rational value) =>
            (ZxvStatus)Native.zxv_emu_rat_add(Sim, (uint)slot, value.Numerator, value.Denominator);

        public ZxvStatus MulRational(int slot, Rational value) =>
            (ZxvStatus)Native.zxv_emu_rat_mul(Sim, (uint)slot, value.Numerator, value.Denominator);

        public Hexagram GetHexagram(int slot)
        {
            ZxvException.ThrowEmu(Native.zxv_emu_hex_get(Sim, (uint)slot, out byte t));
            return Hexagram.FromTag(t);
        }

        public void SetHexagram(int slot, Hexagram value) =>
            ZxvException.ThrowEmu(Native.zxv_emu_hex_set(Sim, (uint)slot, (byte)value.Tag));

        /// <summary>Inside a step: consume from the tick budget. Budget when it would
        /// pass the limit (return it from the step to refuse the tick).</summary>
        public ZxvStatus Charge(Rational amount) =>
            (ZxvStatus)Native.zxv_emu_charge(Sim, amount.Numerator, amount.Denominator);

        // ---- hashes, desync, rollback -------------------------------------------

        /// <summary>The 32-byte state hash of a kept tick, or null when it is not kept.</summary>
        public byte[] Hash(ulong tick) => Hash(tick, out _);

        public byte[] Hash(ulong tick, out bool isFinal)
        {
            var h = new byte[32];
            int rc = Native.zxv_emu_hash(Sim, tick, h, out int f);
            isFinal = rc == 0 && f != 0;
            if (rc == (int)ZxvStatus.NotFound) return null;
            ZxvException.ThrowEmu(rc);
            return h;
        }

        public ulong ConfirmedTick
        {
            get
            {
                ZxvException.ThrowEmu(Native.zxv_emu_confirmed_tick(Sim, out ulong t));
                return t;
            }
        }

        /// <summary>Compare a peer's hash: True (equal), False (a desync, recorded),
        /// Neither (this tick is not final here yet, or not kept).</summary>
        public Truth Compare(ulong tick, byte[] remoteHash)
        {
            if (remoteHash == null || remoteHash.Length != 32)
                throw new ZxvException(-1, "a state hash is 32 bytes");
            ZxvException.ThrowEmu(Native.zxv_emu_compare(Sim, tick, remoteHash, out int lp));
            return (Truth)lp;
        }

        /// <summary>The earliest tick a compare found different, or null.</summary>
        public ulong? DesyncTick
        {
            get
            {
                ZxvException.ThrowEmu(Native.zxv_emu_desync(Sim, out ulong t, out int has));
                return has != 0 ? t : (ulong?)null;
            }
        }

        /// <summary>Predict mode: the earliest tick whose input changed after it was simulated.</summary>
        public ulong? RollbackPendingTick
        {
            get
            {
                ZxvException.ThrowEmu(Native.zxv_emu_rollback_pending(Sim, out ulong t, out int p));
                return p != 0 ? t : (ulong?)null;
            }
        }

        public void Rollback(ulong toTick) => ZxvException.ThrowEmu(Native.zxv_emu_rollback(Sim, toTick));

        /// <summary>If a rollback is pending, roll back and re-run <paramref name="step"/>
        /// up to the current tick. Returns Ok, or the first failing status.</summary>
        public ZxvStatus RollbackReplay(Step step)
        {
            ulong? pending = RollbackPendingTick;
            if (pending == null) return ZxvStatus.Ok;
            ulong target = Tick;
            Rollback(pending.Value - 1);
            while (Tick < target)
            {
                ZxvStatus st = Advance(step);
                if (st != ZxvStatus.Ok) return st;
            }
            return ZxvStatus.Ok;
        }

        // ---- state transfer -----------------------------------------------------

        public byte[] ExportState()
        {
            IntPtr sim = Sim;
            Native.zxv_emu_state_export(sim, null, UIntPtr.Zero, out UIntPtr need);
            var buf = new byte[(int)need];
            ZxvException.ThrowEmu(Native.zxv_emu_state_export(sim, buf, (UIntPtr)buf.Length, out _));
            return buf;
        }

        public void ImportState(byte[] bytes)
        {
            if (bytes == null) throw new ArgumentNullException(nameof(bytes));
            ZxvException.ThrowEmu(Native.zxv_emu_state_import(Sim, bytes, (UIntPtr)bytes.Length));
        }

        /// <summary>Owns the sim and, when a budget is set, its private context and
        /// capabilities; releases them in ABI order (R6): sim, caps, context.</summary>
        private sealed class EmuHandle : SafeHandle
        {
            private IntPtr _ctx, _root, _budget;

            internal EmuHandle(IntPtr sim) : base(IntPtr.Zero, true) { SetHandle(sim); }

            public override bool IsInvalid => handle == IntPtr.Zero;

            internal IntPtr BudgetCap => _budget;

            internal void EnsureBudgetCap()
            {
                if (_budget != IntPtr.Zero) return;
                ZxvException.ThrowZxv(Native.zxv_ctx_create(out _ctx));
                int rc = Native.zxv_cap_create(_ctx, 1, IntPtr.Zero, (1UL << 10) - 1, out _root);
                if (rc == 0) rc = Native.zxv_cap_set_attestation(_root, (int)Truth.True);
                if (rc == 0) rc = Native.zxv_cap_attenuate(_root, 1UL << 5 /* ZXV_CAP_BUDGET */, out _budget);
                if (rc != 0)
                {
                    string detail = Native.ZxvDetail();
                    FreeCaps();
                    throw new ZxvException(rc, detail);
                }
            }

            private void FreeCaps()
            {
                if (_budget != IntPtr.Zero) Native.zxv_cap_destroy(_budget);
                if (_root != IntPtr.Zero) Native.zxv_cap_destroy(_root);
                if (_ctx != IntPtr.Zero) Native.zxv_ctx_destroy(_ctx);
                _budget = _root = _ctx = IntPtr.Zero;
            }

            protected override bool ReleaseHandle()
            {
                Native.zxv_emu_destroy(handle); // releases the budget's rmag first
                FreeCaps();
                return true;
            }
        }
    }
}
