--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
package body Zxv.Authority.Interop is

   function Capability_Handle (Cap : Capability) return System.Address is (Handle_Of (Cap));

end Zxv.Authority.Interop;
