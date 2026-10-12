--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
with Ada.Text_IO;
with Interfaces.C;
with Interfaces.C.Strings;
with System;

package body Zxv is

   use type Interfaces.C.size_t;

   function C_Status_Name (Status : Status_Code) return Interfaces.C.Strings.chars_ptr
     with Import, Convention => C, External_Name => "ze_status_name";
   function C_Status_Class (Status : Status_Code) return Interfaces.Integer_32
     with Import, Convention => C, External_Name => "ze_status_class";
   function C_Last_Error (Buf : System.Address; Cap : Interfaces.C.size_t;
                          Len : access Interfaces.C.size_t) return Status_Code
     with Import, Convention => C, External_Name => "zxv_last_error";
   function C_Pillar_Last_Error (Buf : System.Address; Cap : Interfaces.C.size_t;
                                 Len : access Interfaces.C.size_t) return Status_Code
     with Import, Convention => C, External_Name => "ze_last_error";
   function C_Version (Major, Minor, Patch : access Interfaces.Unsigned_32) return Status_Code
     with Import, Convention => C, External_Name => "zxv_version";
   procedure C_Exit (Status : Interfaces.C.int)
     with Import, Convention => C, External_Name => "exit", No_Return;

   function Status_Name (Status : Status_Code) return String is
   begin
      return Interfaces.C.Strings.Value (C_Status_Name (Status));
   end Status_Name;

   function Class_Of (Status : Status_Code) return Status_Class is
   begin
      case C_Status_Class (Status) is
         when 0      => return Class_Ok;
         when 1      => return Class_Usage;
         when 2      => return Class_Refused;
         when 3      => return Class_Fatal;
         when others => return Class_System;
      end case;
   end Class_Of;

   type Getter is access function (Buf : System.Address; Cap : Interfaces.C.size_t;
                                   Len : access Interfaces.C.size_t) return Status_Code
     with Convention => C;

   function Two_Call (Get : Getter) return String is
      Need : aliased Interfaces.C.size_t := 0;
      Buf  : String (1 .. 600);
   begin
      if Get (Buf'Address, Buf'Length, Need'Access) = Ok then
         return Buf (1 .. Natural (Need));
      elsif Need < 1_000_000 then
         declare
            Big : String (1 .. Natural (Need) + 1);
         begin
            if Get (Big'Address, Big'Length, Need'Access) = Ok then
               return Big (1 .. Natural (Need));
            end if;
         end;
      end if;
      return "";
   end Two_Call;

   function Last_Error return String is (Two_Call (C_Last_Error'Access));
   function Pillar_Last_Error return String is (Two_Call (C_Pillar_Last_Error'Access));

   function C_Text (S : String) return String is (S & ASCII.NUL);

   function Library_Version return ABI_Version is
      A, B, C : aliased Interfaces.Unsigned_32 := 0;
   begin
      Check (C_Version (A'Access, B'Access, C'Access), "Zxv.Library_Version");
      return (Natural (A), Natural (B), Natural (C));
   end Library_Version;

   function Image (Status : Status_Code) return String is
      S : constant String := Status_Code'Image (Status);
   begin
      return S (S'First + (if Status < 0 then 0 else 1) .. S'Last);
   end Image;

   procedure Fatal_Stop (Status : Status_Code; What : String; Detail : String) is
      use Ada.Text_IO;
   begin
      Flush (Standard_Output);
      Put_Line (Standard_Error, "zxv: FATAL " & Status_Name (Status) & " [" & Image (Status) & "] in "
                & What & (if Detail = "" then "" else ": " & Detail));
      Put_Line (Standard_Error, "zxv: the context is halted; stopping (exit"
                & Integer'Image (Fatal_Exit_Status) & ")");
      Flush (Standard_Error);
      C_Exit (Fatal_Exit_Status);
   end Fatal_Stop;

   procedure Check (Status : Status_Code; What : String; Detail : String := "") is
   begin
      if Status = Ok then
         return;
      end if;
      declare
         Why : constant String :=
           (if Detail /= "" then Detail
            elsif Status <= -2201 and Status >= -2299 then Pillar_Last_Error
            else Last_Error);
         Msg : constant String := Status_Name (Status) & " [" & Image (Status) & "] " & What
           & (if Why = "" then "" else ": " & Why);
      begin
         case Class_Of (Status) is
            when Class_Fatal   => Fatal_Stop (Status, What, Why);
            when Class_Usage   => raise Usage_Error with Msg;
            when Class_Refused => raise Refused_Error with Msg;
            when Class_Ok | Class_System => raise System_Error with Msg;
         end case;
      end;
   end Check;

   function Status_Of (Occurrence : Ada.Exceptions.Exception_Occurrence) return Status_Code is
      use Ada.Exceptions;
      Msg : constant String := Exception_Message (Occurrence);
      Id  : constant Exception_Id := Exception_Identity (Occurrence);
   begin
      if Id /= Usage_Error'Identity and Id /= Refused_Error'Identity and Id /= System_Error'Identity then
         return E_Internal;
      end if;
      for I in Msg'Range loop
         if Msg (I) = '[' then
            for J in I + 1 .. Msg'Last loop
               if Msg (J) = ']' then
                  return Status_Code'Value (Msg (I + 1 .. J - 1));
               end if;
            end loop;
         end if;
      end loop;
      return E_Internal;
   exception
      when Constraint_Error =>
         return E_Internal;
   end Status_Of;

end Zxv;
