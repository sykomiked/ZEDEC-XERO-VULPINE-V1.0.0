--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--  zx_legacy.ads — Ada 2012 binding to the ZEDEC legacy bridge C ABI, via
--  Interfaces.C. Imports the stable symbols from libzxlegacy.
with Interfaces.C;
package Zx_Legacy is
   use Interfaces.C;

   function Abi_Version return int
     with Import => True, Convention => C, External_Name => "zx_legacy_abi_version";

   function Hfp64_To_Ieee64 (Hfp : Interfaces.Integer_64) return Interfaces.Integer_64
     with Import => True, Convention => C, External_Name => "zx_hfp64_to_ieee64";

   function Ieee64_To_Hfp64 (Ieee : Interfaces.Integer_64) return Interfaces.Integer_64
     with Import => True, Convention => C, External_Name => "zx_ieee64_to_hfp64";

   function Q850_To_Sip_Status (Cause : unsigned_char) return int
     with Import => True, Convention => C, External_Name => "zx_q850_to_sip_status";
end Zx_Legacy;
