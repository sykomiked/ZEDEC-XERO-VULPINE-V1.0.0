--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  Zxv - the Ada binding of libzxv (ABI 1.3) and of libzeve, the ada-eve
--  pillar. This root package holds the status codes and the error rules;
--  the child packages hold the types:
--
--    Zxv.Rationals            exact int64/int64 rationals
--    Zxv.Logic                four-valued Truth (never a Boolean)
--    Zxv.Hexagrams            the 64 hexagram tags
--    Zxv.Authority            contexts and capabilities (limited, controlled)
--    Zxv.Authority.Ledger     ledger postings through *_cap calls
--    Zxv.Authority.Quarantine the quarantine log
--    Zxv.Eve                  Ada/Eve layouts and their exact values
--    Zxv.Fixed, Zxv.Decimals  integer-with-scale decimals (Zxv.Fixed is
--                             written in the SPARK subset)
--
--  ERRORS. A failing call raises one exception per status class, with a
--  message that starts with the status name and code:
--
--     "ZXV_E_FUNDS [-6] Zxv.Authority.Ledger.Post: ..."
--
--  Status_Of recovers the code from the occurrence. The Try_ forms return
--  a status instead of raising.
--
--  FATAL. A FATAL status (ZXV_E_POISONED, ZXV_E_INVARIANT) is never an
--  exception, in any form: the program prints the reason on standard
--  error and exits with status 70 at once. A poisoned context has seen a
--  broken invariant, nothing on it can succeed again, and an exception
--  could be caught by a "when others" and the books used anyway. This is
--  the Ada counterpart of the Fortran pillar's ERROR STOP 70.
--
--  NO FLOATS. Nothing in this binding has a floating-point type except
--  Zxv.Rationals.From_IEEE_754, the one explicit, exact conversion from a
--  Long_Float (between the R11-BOUNDARY markers; make check-nofloat-ada).
with Ada.Exceptions;
with Interfaces;

package Zxv is

   type Status_Code is new Interfaces.Integer_32;

   --  libzxv (0 .. -99), passed through unchanged
   Ok             : constant Status_Code := 0;
   E_Arg          : constant Status_Code := -1;
   E_Buffer       : constant Status_Code := -2;
   E_Not_Impl     : constant Status_Code := -3;
   E_Not_Found    : constant Status_Code := -4;
   E_Capacity     : constant Status_Code := -5;
   E_Funds        : constant Status_Code := -6;
   E_Currency     : constant Status_Code := -7;
   E_Range        : constant Status_Code := -8;
   E_State        : constant Status_Code := -9;
   E_Invariant    : constant Status_Code := -10;
   E_UTF8         : constant Status_Code := -11;
   E_Too_Long     : constant Status_Code := -12;
   E_Duplicate    : constant Status_Code := -13;
   E_No_Mem       : constant Status_Code := -14;
   E_Declined     : constant Status_Code := -15;
   E_Field        : constant Status_Code := -16;
   E_Cap          : constant Status_Code := -17;
   E_Poisoned     : constant Status_Code := -18;
   E_Budget       : constant Status_Code := -19;
   E_Rmag_Invalid : constant Status_Code := -20;
   E_Verify       : constant Status_Code := -21;
   E_Format       : constant Status_Code := -22;
   E_IO           : constant Status_Code := -23;
   E_Float        : constant Status_Code := -24;
   E_Internal     : constant Status_Code := -99;

   --  ada-eve (-2201 .. -2299)
   ZE_E_Syntax      : constant Status_Code := -2201;
   ZE_E_Unsupported : constant Status_Code := -2202;
   ZE_E_Name        : constant Status_Code := -2203;
   ZE_E_Constraint  : constant Status_Code := -2204;
   ZE_E_Data        : constant Status_Code := -2205;
   ZE_E_Fit         : constant Status_Code := -2206;
   ZE_E_Limit       : constant Status_Code := -2207;

   type Status_Class is (Class_Ok, Class_Usage, Class_Refused, Class_Fatal, Class_System);
   for Status_Class use
     (Class_Ok => 0, Class_Usage => 1, Class_Refused => 2, Class_Fatal => 3, Class_System => 4);

   --  "ZXV_E_FUNDS", "ZE_E_FIT"; libzxv's name for anything else.
   function Status_Name (Status : Status_Code) return String;
   function Class_Of (Status : Status_Code) return Status_Class;

   --  One exception per class. FATAL has none: see above.
   Usage_Error   : exception;   --  a bad argument, a malformed text
   Refused_Error : exception;   --  a well-formed request the library refused
   System_Error  : exception;   --  out of memory, I/O, an internal fault

   --  The status code carried by an occurrence of the three exceptions
   --  above (E_Internal for any other occurrence).
   function Status_Of (Occurrence : Ada.Exceptions.Exception_Occurrence) return Status_Code;

   --  The exit status of a FATAL stop (EX_SOFTWARE).
   Fatal_Exit_Status : constant := 70;

   type ABI_Version is record
      Major, Minor, Patch : Natural;
   end record;
   function Library_Version return ABI_Version;

private

   --  Turn a status into the rule above: Ok returns, FATAL stops the
   --  program, anything else raises with Detail (or the library's last
   --  error) in the message. What names the operation.
   procedure Check (Status : Status_Code; What : String; Detail : String := "");

   --  Print and exit with Fatal_Exit_Status. Never returns, never raises.
   procedure Fatal_Stop (Status : Status_Code; What : String; Detail : String)
     with No_Return;

   --  libzxv's last error detail for the calling thread.
   function Last_Error return String;
   --  libzeve's last error detail for the calling thread.
   function Pillar_Last_Error return String;

   --  A NUL-terminated copy of an Ada string for C.
   function C_Text (S : String) return String
     with Post => C_Text'Result'Length = S'Length + 1;

end Zxv;
