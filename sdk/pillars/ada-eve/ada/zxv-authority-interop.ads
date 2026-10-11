--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  Zxv.Authority.Interop - the C handle behind a capability, for code that
--  calls libzxv or a pillar's C API directly (Zxv.Eve uses it for
--  ze_post). The address is valid only while Cap is held; do not keep it.
--  This is the in-process honest limit made explicit: anything in the
--  process can reach a handle; the capability rules are discipline here
--  and enforcement only across a process boundary (zxvd).
with System;

package Zxv.Authority.Interop is

   function Capability_Handle (Cap : Capability) return System.Address
     with Pre => Is_Held (Cap);

end Zxv.Authority.Interop;
