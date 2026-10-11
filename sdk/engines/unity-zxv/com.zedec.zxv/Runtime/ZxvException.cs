// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;

namespace Zedec.Zxv
{
    /// <summary>Status codes from zxv_api.h that this package can return.</summary>
    public enum ZxvStatus
    {
        Ok = 0,
        Arg = -1,
        Buffer = -2,
        NotImplemented = -3,
        NotFound = -4,
        Capacity = -5,
        Range = -8,
        State = -9,
        Invariant = -10,
        Duplicate = -13,
        NoMemory = -14,
        Capability = -17,
        Poisoned = -18,
        Budget = -19,
        RmagInvalid = -20,
        Format = -22,
        Float = -24,
        Internal = -99,
    }

    /// <summary>
    /// A libzxv or emu-state call failed. <see cref="Status"/> is the code to
    /// branch on; the message carries the native detail text (never branch on
    /// the text).
    /// </summary>
    public class ZxvException : Exception
    {
        public ZxvException(int status, string detail)
            : base(Native.StatusName(status) + (string.IsNullOrEmpty(detail) ? string.Empty : ": " + detail))
        {
            Code = status;
            Detail = detail ?? string.Empty;
        }

        /// <summary>The raw negative status.</summary>
        public int Code { get; }

        /// <summary>The status as an enum (an unknown code is kept as its number).</summary>
        public ZxvStatus Status => (ZxvStatus)Code;

        /// <summary>The native detail text.</summary>
        public string Detail { get; }

        internal static void ThrowZxv(int rc)
        {
            if (rc != 0) throw new ZxvException(rc, Native.ZxvDetail());
        }

        internal static void ThrowEmu(int rc)
        {
            if (rc != 0) throw new ZxvException(rc, Native.EmuDetail());
        }
    }
}
