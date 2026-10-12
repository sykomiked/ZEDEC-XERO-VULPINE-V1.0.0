--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
with Interfaces.C;

package body Zxv.Authority.Ledger is

   use Interfaces;
   use type Interfaces.C.size_t;

   subtype Addr is System.Address;

   function C_Enable (Cap : Addr; Code : Addr) return Status_Code
     with Import, Convention => C, External_Name => "zxv_ccy_enable_cap";
   function C_Enable_Minor (Cap : Addr; Code : Addr; Minor : Unsigned_8) return Status_Code
     with Import, Convention => C, External_Name => "zxv_ccy_enable_with_minor_cap";
   function C_Private (Cap : Addr; Code : Addr; Minor : Unsigned_8) return Status_Code
     with Import, Convention => C, External_Name => "zxv_ccy_register_private_cap";
   function C_Open (Cap : Addr; Name, Ccy : Addr; Kind : Integer_32; Id : access Unsigned_32)
     return Status_Code
     with Import, Convention => C, External_Name => "zxv_ledger_open_account_cap";
   function C_Post (Cap : Addr; From, To : Unsigned_32; Amount : Integer_64; Ccy, Ref : Addr;
                    Id : access Unsigned_64) return Status_Code
     with Import, Convention => C, External_Name => "zxv_ledger_post_cap";
   function C_Balance (Cap : Addr; Account : Unsigned_32; D, C, E : access Integer_64) return Status_Code
     with Import, Convention => C, External_Name => "zxv_ledger_balance_cap";
   function C_Check (Cap : Addr) return Status_Code
     with Import, Convention => C, External_Name => "zxv_ledger_check_cap";
   function C_Count (Cap : Addr; N : access Unsigned_32) return Status_Code
     with Import, Convention => C, External_Name => "zxv_ledger_account_count_cap";
   function C_Name (Cap : Addr; Account : Unsigned_32; Buf : Addr; Len : Interfaces.C.size_t;
                    Out_Len : access Interfaces.C.size_t) return Status_Code
     with Import, Convention => C, External_Name => "zxv_ledger_account_name_cap";
   function C_Ccy (Cap : Addr; Account : Unsigned_32; Buf : Addr; Len : Interfaces.C.size_t;
                   Out_Len : access Interfaces.C.size_t) return Status_Code
     with Import, Convention => C, External_Name => "zxv_ledger_account_ccy_cap";
   function C_Kind (Cap : Addr; Account : Unsigned_32; K : access Integer_32) return Status_Code
     with Import, Convention => C, External_Name => "zxv_ledger_account_kind_cap";
   function C_Entry_Count (Cap : Addr; Account : Unsigned_32; N : access Unsigned_32) return Status_Code
     with Import, Convention => C, External_Name => "zxv_ledger_entry_count_cap";
   function C_Entry_At (Cap : Addr; Account, Index : Unsigned_32; Id : access Unsigned_64;
                        Amount : access Integer_64; Party : access Unsigned_32; Ref : Addr;
                        Ref_Cap : Interfaces.C.size_t; Ref_Len : access Interfaces.C.size_t) return Status_Code
     with Import, Convention => C, External_Name => "zxv_ledger_entry_at_cap";

   procedure Enable_Currency (Cap : Capability; Code : String) is
      C : constant String := C_Text (Code);
   begin
      Check (C_Enable (Handle_Of (Cap), C'Address), "Zxv.Authority.Ledger.Enable_Currency");
   end Enable_Currency;

   procedure Enable_Currency (Cap : Capability; Code : String; Minor : Natural) is
      C : constant String := C_Text (Code);
   begin
      Check (C_Enable_Minor (Handle_Of (Cap), C'Address, Unsigned_8 (Minor)),
             "Zxv.Authority.Ledger.Enable_Currency");
   end Enable_Currency;

   procedure Register_Private_Unit (Cap : Capability; Code : String; Minor : Natural) is
      C : constant String := C_Text (Code);
   begin
      Check (C_Private (Handle_Of (Cap), C'Address, Unsigned_8 (Minor)),
             "Zxv.Authority.Ledger.Register_Private_Unit");
   end Register_Private_Unit;

   function Open_Account (Cap : Capability; Name : String; Ccy : String; Kind : Account_Kind)
     return Account_Id
   is
      N  : constant String := C_Text (Name);
      C  : constant String := C_Text (Ccy);
      Id : aliased Unsigned_32 := 0;
   begin
      Check (C_Open (Handle_Of (Cap), N'Address, C'Address, Account_Kind'Enum_Rep (Kind), Id'Access),
             "Zxv.Authority.Ledger.Open_Account");
      return Account_Id (Id);
   end Open_Account;

   procedure Try_Post (Cap : Capability; From, To : Account_Id; Amount : Minor_Units;
                       Ccy : String; Reference : String; Id : out Entry_Id; Status : out Status_Code) is
      C : constant String := C_Text (Ccy);
      R : constant String := C_Text (Reference);
      E : aliased Unsigned_64 := 0;
   begin
      Status := C_Post (Handle_Of (Cap), Unsigned_32 (From), Unsigned_32 (To), Amount, C'Address, R'Address,
                        E'Access);
      Id := (if Status = Ok then Entry_Id (E) else 0);
      if Class_Of (Status) = Class_Fatal then
         Check (Status, "Zxv.Authority.Ledger.Try_Post", "posting " & Reference);
      end if;
   end Try_Post;

   function Post (Cap : Capability; From, To : Account_Id; Amount : Minor_Units;
                  Ccy : String; Reference : String) return Entry_Id is
      Id : Entry_Id;
      S  : Status_Code;
   begin
      Try_Post (Cap, From, To, Amount, Ccy, Reference, Id, S);
      Check (S, "Zxv.Authority.Ledger.Post");
      return Id;
   end Post;

   function Balance_Of (Cap : Capability; Account : Account_Id) return Balance is
      D, C, E : aliased Integer_64 := 0;
   begin
      Check (C_Balance (Handle_Of (Cap), Unsigned_32 (Account), D'Access, C'Access, E'Access),
             "Zxv.Authority.Ledger.Balance_Of");
      return (D, C, E);
   end Balance_Of;

   procedure Verify (Cap : Capability) is
   begin
      Check (C_Check (Handle_Of (Cap)), "Zxv.Authority.Ledger.Verify");
   end Verify;

   function Account_Count (Cap : Capability) return Natural is
      N : aliased Unsigned_32 := 0;
   begin
      Check (C_Count (Handle_Of (Cap), N'Access), "Zxv.Authority.Ledger.Account_Count");
      return Natural (N);
   end Account_Count;

   type Text_Getter is access function (Cap : Addr; Account : Unsigned_32; Buf : Addr;
                                        Len : Interfaces.C.size_t;
                                        Out_Len : access Interfaces.C.size_t) return Status_Code
     with Convention => C;

   function Text (Get : Text_Getter; Cap : Capability; Account : Account_Id; What : String) return String is
      Buf : String (1 .. 80);
      Len : aliased Interfaces.C.size_t := 0;
   begin
      Check (Get (Handle_Of (Cap), Unsigned_32 (Account), Buf'Address, Buf'Length, Len'Access), What);
      return Buf (1 .. Natural (Len));
   end Text;

   function Account_Name (Cap : Capability; Account : Account_Id) return String is
     (Text (C_Name'Access, Cap, Account, "Zxv.Authority.Ledger.Account_Name"));
   function Account_Currency (Cap : Capability; Account : Account_Id) return String is
     (Text (C_Ccy'Access, Cap, Account, "Zxv.Authority.Ledger.Account_Currency"));

   function Kind_Of (Cap : Capability; Account : Account_Id) return Account_Kind is
      K : aliased Integer_32 := 0;
   begin
      Check (C_Kind (Handle_Of (Cap), Unsigned_32 (Account), K'Access), "Zxv.Authority.Ledger.Kind_Of");
      return (if K = 1 then Issuer else Holder);
   end Kind_Of;

   function Entry_Count (Cap : Capability; Account : Account_Id) return Natural is
      N : aliased Unsigned_32 := 0;
   begin
      Check (C_Entry_Count (Handle_Of (Cap), Unsigned_32 (Account), N'Access), "Zxv.Authority.Ledger.Entry_Count");
      return Natural (N);
   end Entry_Count;

   function Entry_At (Cap : Capability; Account : Account_Id; Index : Natural) return Journal_Line is
      Id    : aliased Unsigned_64 := 0;
      Amt   : aliased Integer_64 := 0;
      Party : aliased Unsigned_32 := 0;
      Ref   : String (1 .. 36) := (others => ' ');
      Len   : aliased Interfaces.C.size_t := 0;
      Line  : Journal_Line;
   begin
      Check (C_Entry_At (Handle_Of (Cap), Unsigned_32 (Account), Unsigned_32 (Index), Id'Access, Amt'Access,
                         Party'Access, Ref'Address, Ref'Length, Len'Access), "Zxv.Authority.Ledger.Entry_At");
      Line.Id := Entry_Id (Id);
      Line.Amount := Amt;
      Line.Counterparty := Account_Id (Party);
      Line.Reference_Length := Natural'Min (Natural (Len), 35);
      Line.Reference := (others => ' ');
      Line.Reference (1 .. Line.Reference_Length) := Ref (1 .. Line.Reference_Length);
      return Line;
   end Entry_At;

end Zxv.Authority.Ledger;
