--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  Zxv.Authority.Ledger - the libzxv ledger, only through capabilities
--  (the zxv_ledger_*_cap calls of ABI 1.2+). Opening accounts and posting
--  need Ledger_Post; reading needs Ledger_Read. Every refusal is the
--  ledger's (ZXV_E_FUNDS, ZXV_E_DUPLICATE, ZXV_E_CURRENCY, ZXV_E_CAP, ...):
--  this package passes it on, as an exception or, from Try_Post, as a
--  status. ZXV_E_POISONED is FATAL in both forms.
with Interfaces;

package Zxv.Authority.Ledger is

   use type Interfaces.Integer_64;

   type Account_Id is new Interfaces.Unsigned_32;
   type Entry_Id is new Interfaces.Unsigned_64;

   type Account_Kind is (Holder, Issuer);
   for Account_Kind use (Holder => 0, Issuer => 1);
   --  A Holder may never go below zero; an Issuer may carry a credit.

   subtype Minor_Units is Interfaces.Integer_64;
   --  The largest amount in one posting and the largest balance (2**53 - 1).
   Amount_Max : constant Minor_Units := 9_007_199_254_740_991;

   procedure Enable_Currency (Cap : Capability; Code : String)
     with Pre => Is_Held (Cap) and Code'Length in 1 .. 15;
   procedure Enable_Currency (Cap : Capability; Code : String; Minor : Natural)
     with Pre => Is_Held (Cap) and Code'Length in 1 .. 15 and Minor <= 18;
   --  A platform-private unit (not ISO 4217) with its minor units.
   procedure Register_Private_Unit (Cap : Capability; Code : String; Minor : Natural)
     with Pre => Is_Held (Cap) and Code'Length in 1 .. 15 and Minor <= 18;

   function Open_Account (Cap : Capability; Name : String; Ccy : String; Kind : Account_Kind)
     return Account_Id
     with Pre => Is_Held (Cap) and Name'Length in 1 .. 63 and Ccy'Length in 1 .. 15;

   --  A balanced transfer of Amount minor units. Reference (at most 35
   --  bytes) must be unique in the context.
   function Post (Cap : Capability; From, To : Account_Id; Amount : Minor_Units;
                  Ccy : String; Reference : String) return Entry_Id
     with Pre => Is_Held (Cap) and Ccy'Length in 1 .. 15 and Reference'Length <= 35;
   procedure Try_Post (Cap : Capability; From, To : Account_Id; Amount : Minor_Units;
                       Ccy : String; Reference : String; Id : out Entry_Id; Status : out Status_Code)
     with Pre => Is_Held (Cap) and Ccy'Length in 1 .. 15 and Reference'Length <= 35;

   type Balance is record
      Debit, Credit, Equity : Minor_Units;
   end record;
   --  Rail balances: Debit (555, received), Credit (777, sent), Equity
   --  (888 = Debit - Credit, the spendable position).
   function Balance_Of (Cap : Capability; Account : Account_Id) return Balance
     with Pre => Is_Held (Cap), Post => Balance_Of'Result.Equity = Balance_Of'Result.Debit - Balance_Of'Result.Credit;

   --  zxv_ledger_check_cap: per currency debits = credits, every holder
   --  >= 0, the exact shadow agrees. A failure poisons the context and is
   --  FATAL.
   procedure Verify (Cap : Capability)
     with Pre => Is_Held (Cap);

   function Account_Count (Cap : Capability) return Natural
     with Pre => Is_Held (Cap);
   function Account_Name (Cap : Capability; Account : Account_Id) return String
     with Pre => Is_Held (Cap);
   function Account_Currency (Cap : Capability; Account : Account_Id) return String
     with Pre => Is_Held (Cap);
   function Kind_Of (Cap : Capability; Account : Account_Id) return Account_Kind
     with Pre => Is_Held (Cap);

   type Journal_Line is record
      Id               : Entry_Id;
      Amount           : Minor_Units;  --  + received, - sent
      Counterparty     : Account_Id;
      Reference        : String (1 .. 35);
      Reference_Length : Natural;
   end record;

   function Entry_Count (Cap : Capability; Account : Account_Id) return Natural
     with Pre => Is_Held (Cap);
   function Entry_At (Cap : Capability; Account : Account_Id; Index : Natural) return Journal_Line
     with Pre => Is_Held (Cap);

end Zxv.Authority.Ledger;
