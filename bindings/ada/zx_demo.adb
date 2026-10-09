--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--  zx_demo.adb — Ada example calling the ZEDEC legacy bridge: HFP<->IEEE and
--  a Q.850->SIP cause mapping. Exits non-zero on mismatch.
with Ada.Text_IO;         use Ada.Text_IO;
with Ada.Command_Line;    use Ada.Command_Line;
with Interfaces;          use Interfaces;
with Interfaces.C;        use Interfaces.C;
with Zx_Legacy;           use Zx_Legacy;
procedure Zx_Demo is
   One_Hfp  : constant Interfaces.Integer_64 := 16#4110000000000000#;
   One_Ieee : constant Interfaces.Integer_64 := 16#3FF0000000000000#;
   Got      : Interfaces.Integer_64;
   Ok       : Boolean := True;
begin
   Put_Line ("ZEDEC legacy ABI version:" & int'Image (Abi_Version));

   Got := Hfp64_To_Ieee64 (One_Hfp);
   if Got /= One_Ieee then Ok := False; end if;
   if Ieee64_To_Hfp64 (One_Ieee) /= One_Hfp then Ok := False; end if;

   --  Q.850 cause 17 (user busy) maps to SIP 486
   if Q850_To_Sip_Status (17) /= 486 then Ok := False; end if;

   if Ok then
      Put_Line ("Ada binding checks OK");
      Set_Exit_Status (0);
   else
      Put_Line ("Ada binding MISMATCH");
      Set_Exit_Status (1);
   end if;
end Zx_Demo;
