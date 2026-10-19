--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  test_support - the check counter shared by the Ada tests.
with Zxv;

package Test_Support is

   procedure Check (Condition : Boolean; What : String);

   --  Action must raise Usage_Error, Refused_Error or System_Error with
   --  status Want; anything else (including no exception) is a failure.
   procedure Raises (What : String; Want : Zxv.Status_Code; Action : not null access procedure);

   --  Print "NAME: N checks, F failed" and set the exit status.
   procedure Finish (Name : String);

end Test_Support;
