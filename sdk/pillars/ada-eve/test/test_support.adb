--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
with Ada.Command_Line;
with Ada.Exceptions;
with Ada.Text_IO;

package body Test_Support is

   use type Zxv.Status_Code;

   Checks, Fails : Natural := 0;

   procedure Check (Condition : Boolean; What : String) is
   begin
      Checks := Checks + 1;
      if not Condition then
         Fails := Fails + 1;
         Ada.Text_IO.Put_Line ("FAIL: " & What);
      end if;
   end Check;

   procedure Raises (What : String; Want : Zxv.Status_Code; Action : not null access procedure) is
   begin
      Action.all;
      Check (False, What & ": no exception (want " & Zxv.Status_Name (Want) & ")");
   exception
      when E : Zxv.Usage_Error | Zxv.Refused_Error | Zxv.System_Error =>
         Check (Zxv.Status_Of (E) = Want, What & ": got " & Ada.Exceptions.Exception_Message (E)
                & " (want " & Zxv.Status_Name (Want) & ")");
   end Raises;

   procedure Finish (Name : String) is
   begin
      Ada.Text_IO.Put_Line (Name & ":" & Natural'Image (Checks) & " checks," & Natural'Image (Fails) & " failed");
      Ada.Command_Line.Set_Exit_Status (if Fails = 0 then Ada.Command_Line.Success else Ada.Command_Line.Failure);
   end Finish;

end Test_Support;
