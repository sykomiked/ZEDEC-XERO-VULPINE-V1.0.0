// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Zedec.Zxv
{
    /// <summary>
    /// P/Invoke declarations for libzxv (zxv_api.h) and emu-state (zxv_emu.h).
    /// Every signature is blittable (integers, IntPtr, byte arrays), so the
    /// same code runs under Mono, IL2CPP and CoreCLR. No callback crosses the
    /// boundary: ticks are driven from C# (begin, step, commit), so IL2CPP
    /// needs no reverse P/Invoke.
    /// </summary>
    internal static class Native
    {
#if UNITY_IOS && !UNITY_EDITOR
        // iOS links native code statically into the player.
        internal const string Zxv = "__Internal";
        internal const string Emu = "__Internal";
#else
        internal const string Zxv = "zxv";
        internal const string Emu = "zxv_emu";
#endif

        // ---- libzxv -----------------------------------------------------
        [DllImport(Zxv)] internal static extern int zxv_version(out uint major, out uint minor, out uint patch);
        [DllImport(Zxv)] internal static extern IntPtr zxv_status_name(int status);
        [DllImport(Zxv)] internal static extern int zxv_last_error(byte[] buf, UIntPtr cap, out UIntPtr outLen);

        [DllImport(Zxv)] internal static extern int zxv_rat_normalize(long num, long den, out long outNum, out long outDen);
        [DllImport(Zxv)] internal static extern int zxv_rat_add(long an, long ad, long bn, long bd, out long outNum, out long outDen);
        [DllImport(Zxv)] internal static extern int zxv_rat_sub(long an, long ad, long bn, long bd, out long outNum, out long outDen);
        [DllImport(Zxv)] internal static extern int zxv_rat_mul(long an, long ad, long bn, long bd, out long outNum, out long outDen);
        [DllImport(Zxv)] internal static extern int zxv_rat_div(long an, long ad, long bn, long bd, out long outNum, out long outDen);
        [DllImport(Zxv)] internal static extern int zxv_rat_cmp(long an, long ad, long bn, long bd, out int outCmp);
        [DllImport(Zxv)] internal static extern int zxv_rat_parse(byte[] utf8z, out long outNum, out long outDen);
        [DllImport(Zxv)] internal static extern int zxv_rat_format(long num, long den, [Out] byte[] buf, UIntPtr cap, out UIntPtr outLen);

        [DllImport(Zxv)] internal static extern int zxv_hex_pack(byte b, byte polarity, byte phase, out byte tag);
        [DllImport(Zxv)] internal static extern int zxv_hex_unpack(byte tag, out byte b, out byte polarity, out byte phase);
        [DllImport(Zxv)] internal static extern int zxv_hex_action(byte tag, out int action);
        [DllImport(Zxv)] internal static extern int zxv_hex_phase_next(byte phase, out byte next);
        [DllImport(Zxv)] internal static extern int zxv_hex_step(byte tag, byte phase, out byte outTag);
        [DllImport(Zxv)] internal static extern int zxv_hex_set(byte tag, byte b, byte polarity, out byte outTag);
        [DllImport(Zxv)] internal static extern int zxv_hex_forbid(byte tag, out byte outTag);
        [DllImport(Zxv)] internal static extern int zxv_hex_and(byte a, byte b, out byte outTag);
        [DllImport(Zxv)] internal static extern int zxv_hex_or(byte a, byte b, out byte outTag);
        [DllImport(Zxv)] internal static extern int zxv_hex_not(byte a, out byte outTag);
        [DllImport(Zxv)] internal static extern int zxv_hex_from_lp(int lp, out byte b);
        [DllImport(Zxv)] internal static extern int zxv_hex_to_lp(byte b, out int lp);
        [DllImport(Zxv)] internal static extern IntPtr zxv_hex_name(byte tag);
        [DllImport(Zxv)] internal static extern IntPtr zxv_hex_action_name(int action);

        [DllImport(Zxv)] internal static extern int zxv_ctx_create(out IntPtr ctx);
        [DllImport(Zxv)] internal static extern void zxv_ctx_destroy(IntPtr ctx);
        [DllImport(Zxv)] internal static extern int zxv_cap_create(IntPtr ctx, uint processId, IntPtr identity, ulong caps, out IntPtr cap);
        [DllImport(Zxv)] internal static extern int zxv_cap_attenuate(IntPtr parent, ulong keep, out IntPtr cap);
        [DllImport(Zxv)] internal static extern int zxv_cap_set_attestation(IntPtr cap, int lp);
        [DllImport(Zxv)] internal static extern void zxv_cap_destroy(IntPtr cap);

        // ---- emu-state ----------------------------------------------------
        [DllImport(Emu)] internal static extern int zxv_emu_create(uint players, uint ratSlots, uint hexSlots, uint inputBytes, uint history, uint flags, out IntPtr sim);
        [DllImport(Emu)] internal static extern void zxv_emu_destroy(IntPtr sim);
        [DllImport(Emu)] internal static extern int zxv_emu_last_error(byte[] buf, UIntPtr cap, out UIntPtr outLen);
        [DllImport(Emu)] internal static extern int zxv_emu_set_tick_budget(IntPtr sim, IntPtr cap, long num, long den);
        [DllImport(Emu)] internal static extern int zxv_emu_start(IntPtr sim);
        [DllImport(Emu)] internal static extern int zxv_emu_clock_set_step(IntPtr sim, long num, long den, uint maxCatchup);
        [DllImport(Emu)] internal static extern int zxv_emu_clock_feed(IntPtr sim, long num, long den, out uint due, out ulong dropped);
        [DllImport(Emu)] internal static extern int zxv_emu_clock_alpha(IntPtr sim, out long num, out long den);
        [DllImport(Emu)] internal static extern int zxv_emu_input_submit(IntPtr sim, uint player, ulong tick, byte[] bytes, UIntPtr len);
        [DllImport(Emu)] internal static extern int zxv_emu_input_get(IntPtr sim, uint player, [Out] byte[] buf, UIntPtr cap, out UIntPtr outLen, out int predicted);
        [DllImport(Emu)] internal static extern int zxv_emu_ready(IntPtr sim, out int ready);
        [DllImport(Emu)] internal static extern int zxv_emu_tick(IntPtr sim, out ulong tick);
        [DllImport(Emu)] internal static extern int zxv_emu_begin(IntPtr sim);
        [DllImport(Emu)] internal static extern int zxv_emu_commit(IntPtr sim);
        [DllImport(Emu)] internal static extern int zxv_emu_abort(IntPtr sim);
        [DllImport(Emu)] internal static extern int zxv_emu_rat_get(IntPtr sim, uint slot, out long num, out long den);
        [DllImport(Emu)] internal static extern int zxv_emu_rat_set(IntPtr sim, uint slot, long num, long den);
        [DllImport(Emu)] internal static extern int zxv_emu_rat_add(IntPtr sim, uint slot, long num, long den);
        [DllImport(Emu)] internal static extern int zxv_emu_rat_mul(IntPtr sim, uint slot, long num, long den);
        [DllImport(Emu)] internal static extern int zxv_emu_hex_get(IntPtr sim, uint slot, out byte tag);
        [DllImport(Emu)] internal static extern int zxv_emu_hex_set(IntPtr sim, uint slot, byte tag);
        [DllImport(Emu)] internal static extern int zxv_emu_charge(IntPtr sim, long num, long den);
        [DllImport(Emu)] internal static extern int zxv_emu_hash(IntPtr sim, ulong tick, [Out] byte[] out32, out int final);
        [DllImport(Emu)] internal static extern int zxv_emu_confirmed_tick(IntPtr sim, out ulong tick);
        [DllImport(Emu)] internal static extern int zxv_emu_compare(IntPtr sim, ulong tick, byte[] remote32, out int lp);
        [DllImport(Emu)] internal static extern int zxv_emu_desync(IntPtr sim, out ulong tick, out int has);
        [DllImport(Emu)] internal static extern int zxv_emu_rollback_pending(IntPtr sim, out ulong tick, out int pending);
        [DllImport(Emu)] internal static extern int zxv_emu_rollback(IntPtr sim, ulong toTick);
        [DllImport(Emu)] internal static extern int zxv_emu_state_export(IntPtr sim, [Out] byte[] buf, UIntPtr cap, out UIntPtr outLen);
        [DllImport(Emu)] internal static extern int zxv_emu_state_import(IntPtr sim, byte[] buf, UIntPtr len);

        // ---- helpers --------------------------------------------------------
        internal static string StatusName(int status)
        {
            IntPtr p = zxv_status_name(status);
            return p == IntPtr.Zero ? "ZXV_E_UNKNOWN" : Marshal.PtrToStringAnsi(p) ?? "ZXV_E_UNKNOWN";
        }

        internal static string Ansi(IntPtr p) => p == IntPtr.Zero ? string.Empty : Marshal.PtrToStringAnsi(p) ?? string.Empty;

        internal static string ZxvDetail()
        {
            var buf = new byte[512];
            return zxv_last_error(buf, (UIntPtr)buf.Length, out UIntPtr n) == 0
                ? Encoding.UTF8.GetString(buf, 0, (int)n)
                : string.Empty;
        }

        internal static string EmuDetail()
        {
            var buf = new byte[512];
            return zxv_emu_last_error(buf, (UIntPtr)buf.Length, out UIntPtr n) == 0
                ? Encoding.UTF8.GetString(buf, 0, (int)n)
                : string.Empty;
        }

        internal static byte[] Utf8Z(string s)
        {
            int n = Encoding.UTF8.GetByteCount(s);
            var b = new byte[n + 1];
            Encoding.UTF8.GetBytes(s, 0, s.Length, b, 0);
            return b;
        }

        private static bool s_checked;

        /// <summary>Refuses a libzxv whose major is not 1 or whose minor is below 3.</summary>
        internal static void CheckVersion()
        {
            if (s_checked) return;
            int rc = zxv_version(out uint major, out uint minor, out _);
            if (rc != 0 || major != 1 || minor < 3)
                throw new ZxvException(-3, "libzxv " + major + "." + minor + " loaded; 1.3 or later (major 1) is required");
            s_checked = true;
        }
    }
}
