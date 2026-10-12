--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  Zxv.Eve - Ada type declarations and their Eve shorthand as one layout
--  (libzeve). A Layout is parsed from either form; both give the same
--  canonical text and the same SHA3-256 digest when they declare the same
--  types. Values of a type are Bytes in the canonical encoding (big-endian,
--  packed, README.md "Encoding"), read and written exactly by path:
--
--     L.Parse_Eve ("eve pay" & LF & "money = dec(15, 2)" & LF & ...);
--     V : Bytes := L.New_Value (L.Find ("payment"));
--     L.Put_Text (T, V, "amount", "1234.56");
--     A : Zxv.Fixed.Decimal := L.Get_Decimal (T, V, "amount");  -- 123456, scale 2
--
--  The encoding is the pillar's, not GNAT's in-memory representation: a
--  record is exchanged as these bytes, never by overlaying an Ada object.
with Interfaces;
with Zxv.Authority;
with Zxv.Authority.Ledger;
with Zxv.Fixed;
with Zxv.Rationals;
private with Ada.Finalization;
private with Ada.Strings.Unbounded;
private with System;

package Zxv.Eve is

   use type Interfaces.Unsigned_32;
   use type Interfaces.Unsigned_64;

   type Bytes is array (Positive range <>) of Interfaces.Unsigned_8;
   type Digest is array (1 .. 32) of Interfaces.Unsigned_8;

   type Layout is tagged limited private;

   function Is_Loaded (L : Layout) return Boolean;

   --  Parse an Ada package specification or Eve text. A refusal raises
   --  Usage_Error (ZE_E_SYNTAX, ZE_E_UNSUPPORTED, ZE_E_NAME,
   --  ZE_E_CONSTRAINT) or Refused_Error (ZE_E_LIMIT) whose message holds
   --  "line L, column C: why".
   procedure Parse_Ada (L : in out Layout; Text : String)
     with Pre => not Is_Loaded (L), Post => Is_Loaded (L);
   procedure Parse_Eve (L : in out Layout; Text : String)
     with Pre => not Is_Loaded (L), Post => Is_Loaded (L);
   --  A file: .ads is Ada, anything else is Eve.
   procedure Load (L : in out Layout; Path : String)
     with Pre => not Is_Loaded (L), Post => Is_Loaded (L);

   type Source_Form is (Ada_Spec, Eve_Text);
   type Diagnostic is record
      Line, Column : Natural := 0;
      Message      : String (1 .. 255) := (others => ' ');
      Length       : Natural := 0;
   end record;
   --  The status form: Status is Ok (and L is loaded) or the refusal, with
   --  Where filled in for a parse refusal.
   procedure Try_Parse (L : in out Layout; Text : String; Form : Source_Form;
                        Status : out Status_Code; Where : out Diagnostic)
     with Pre => not Is_Loaded (L), Post => Is_Loaded (L) = (Status = Ok);

   procedure Unload (L : in out Layout)
     with Post => not Is_Loaded (L);

   function Name (L : Layout) return String with Pre => Is_Loaded (L);
   --  The canonical Eve text and the same package printed as Ada.
   function Canonical (L : Layout) return String with Pre => Is_Loaded (L);
   function As_Ada (L : Layout) return String with Pre => Is_Loaded (L);
   function Package_Digest (L : Layout) return Digest with Pre => Is_Loaded (L);
   function Hex (D : Digest) return String with Post => Hex'Result'Length = 64;
   --  The two declare the same types (their digests are equal).
   function Same (A, B : Layout) return Boolean
     with Pre => Is_Loaded (A) and Is_Loaded (B);

   ----------------------------------------------------------------------
   --  Types
   ----------------------------------------------------------------------

   type Type_Ref is new Interfaces.Unsigned_32;
   Boolean_Type : constant Type_Ref := 0;
   No_Index     : constant Type_Ref := Type_Ref'Last;  --  a literal index range

   type Kind is (Integer_Kind, Decimal_Kind, Enumeration_Kind, Text_Kind, Array_Kind, Record_Kind);

   function Type_Count (L : Layout) return Natural with Pre => Is_Loaded (L);
   --  A named type, case-insensitive; Usage_Error (ZXV_E_NOT_FOUND) if none.
   function Find (L : Layout; Type_Name : String) return Type_Ref with Pre => Is_Loaded (L);

   type Type_Info is record
      Name        : String (1 .. 63);
      Name_Length : Natural;          --  0 for an anonymous type
      Of_Kind     : Kind;
      Is_Subtype  : Boolean;
      Base        : Type_Ref;
      Lo, Hi      : Interfaces.Integer_64;  --  in units of 10**(-Scale); positions
      Digits_Count, Scale : Natural;
      Count       : Natural;  --  literals, characters, elements, components
      Element     : Type_Ref;
      Dimensions  : Natural;
      Size        : Natural;  --  bytes of one value
   end record;
   function Info (L : Layout; T : Type_Ref) return Type_Info with Pre => Is_Loaded (L);
   function Size (L : Layout; T : Type_Ref) return Natural with Pre => Is_Loaded (L);

   type Dimension_Info is record
      Index  : Type_Ref;   --  No_Index for a literal range
      Lo, Hi : Interfaces.Integer_64;
   end record;
   function Dimension (L : Layout; T : Type_Ref; D : Positive) return Dimension_Info
     with Pre => Is_Loaded (L);

   type Component_Info is record
      Name        : String (1 .. 63);
      Name_Length : Natural;
      Of_Type     : Type_Ref;
      Offset      : Natural;
   end record;
   function Component (L : Layout; T : Type_Ref; Index : Positive) return Component_Info
     with Pre => Is_Loaded (L);
   function Literal (L : Layout; T : Type_Ref; Position : Natural) return String
     with Pre => Is_Loaded (L);

   ----------------------------------------------------------------------
   --  Values. Path "" is the whole value, "amount" a component,
   --  "lines(3)" an element, "grid(2, usd).qty" both. A value that does not
   --  fit the declared range or scale raises Refused_Error (ZE_E_FIT) and
   --  leaves V unchanged; nothing is rounded or truncated.
   ----------------------------------------------------------------------

   --  A value holding the canonical initial value (zeros or the value
   --  nearest zero, first literals, spaces).
   function New_Value (L : Layout; T : Type_Ref) return Bytes
     with Pre => Is_Loaded (L), Post => New_Value'Result'Length = Size (L, T);
   --  Refused_Error (ZE_E_DATA) naming the first scalar out of range.
   procedure Check_Value (L : Layout; T : Type_Ref; V : Bytes)
     with Pre => Is_Loaded (L) and then V'Length >= Size (L, T);
   function Is_Valid (L : Layout; T : Type_Ref; V : Bytes) return Boolean
     with Pre => Is_Loaded (L) and then V'Length >= Size (L, T);

   function Get_Decimal (L : Layout; T : Type_Ref; V : Bytes; Path : String) return Zxv.Fixed.Decimal
     with Pre => Is_Loaded (L) and then V'Length >= Size (L, T);
   function Get_Integer (L : Layout; T : Type_Ref; V : Bytes; Path : String) return Interfaces.Integer_64
     with Pre => Is_Loaded (L) and then V'Length >= Size (L, T);
   function Get_Rational (L : Layout; T : Type_Ref; V : Bytes; Path : String) return Zxv.Rationals.Rational
     with Pre => Is_Loaded (L) and then V'Length >= Size (L, T);
   function Get_Position (L : Layout; T : Type_Ref; V : Bytes; Path : String) return Natural
     with Pre => Is_Loaded (L) and then V'Length >= Size (L, T);
   --  Any scalar as text: "42", "-123.45", "usd", or a String's bytes.
   function Get_Text (L : Layout; T : Type_Ref; V : Bytes; Path : String) return String
     with Pre => Is_Loaded (L) and then V'Length >= Size (L, T);

   procedure Put_Decimal (L : Layout; T : Type_Ref; V : in out Bytes; Path : String;
                          Value : Zxv.Fixed.Decimal)
     with Pre => Is_Loaded (L) and then V'Length >= Size (L, T);
   procedure Put_Integer (L : Layout; T : Type_Ref; V : in out Bytes; Path : String;
                          Value : Interfaces.Integer_64)
     with Pre => Is_Loaded (L) and then V'Length >= Size (L, T);
   procedure Put_Rational (L : Layout; T : Type_Ref; V : in out Bytes; Path : String;
                           Value : Zxv.Rationals.Rational)
     with Pre => Is_Loaded (L) and then V'Length >= Size (L, T);
   procedure Put_Position (L : Layout; T : Type_Ref; V : in out Bytes; Path : String;
                           Position : Natural)
     with Pre => Is_Loaded (L) and then V'Length >= Size (L, T);
   procedure Put_Text (L : Layout; T : Type_Ref; V : in out Bytes; Path : String; Text : String)
     with Pre => Is_Loaded (L) and then V'Length >= Size (L, T);

   ----------------------------------------------------------------------
   --  Postings: which components of a record make one ledger transfer.
   ----------------------------------------------------------------------

   type Posting_Map is private;
   subtype Minor_Digits is Integer range -1 .. 18;
   --  Amount, From and To are number paths (the accounts whole numbers);
   --  Reference a String path. The currency is the String or enumeration
   --  at Currency, or Fixed_Currency when Currency is "". Minor_Units -1
   --  takes the currency's minor units from ISO 4217.
   function Map (Amount, From, To, Reference : String;
                 Currency       : String := "";
                 Fixed_Currency : String := "";
                 Minor_Units    : Minor_Digits := -1) return Posting_Map
     with Pre => Amount /= "" and From /= "" and To /= "" and Reference /= ""
                 and (Currency /= "" or Fixed_Currency /= "") and Fixed_Currency'Length <= 7;

   type Posting is record
      Amount_Minor     : Zxv.Authority.Ledger.Minor_Units;
      From, To         : Zxv.Authority.Ledger.Account_Id;
      Currency         : String (1 .. 7);
      Currency_Length  : Natural;
      Reference        : String (1 .. 35);
      Reference_Length : Natural;
      Entry_Ref        : Zxv.Authority.Ledger.Entry_Id;  --  0 until posted
   end record;

   --  Read the posting a record holds, touching no ledger.
   function Read_Posting (L : Layout; T : Type_Ref; Map : Posting_Map; V : Bytes) return Posting
     with Pre => Is_Loaded (L) and then V'Length >= Size (L, T);
   --  Read and post it through zxv_ledger_post_cap (needs Ledger_Post).
   --  Refusals raise as usual; ZXV_E_POISONED stops the program (exit 70).
   function Post (Cap : Zxv.Authority.Capability; L : Layout; T : Type_Ref; Map : Posting_Map;
                  V : Bytes) return Posting
     with Pre  => Zxv.Authority.Is_Held (Cap) and then Is_Loaded (L) and then V'Length >= Size (L, T);
   procedure Try_Post (Cap : Zxv.Authority.Capability; L : Layout; T : Type_Ref; Map : Posting_Map;
                       V : Bytes; Result : out Posting; Status : out Status_Code)
     with Pre  => Zxv.Authority.Is_Held (Cap) and then Is_Loaded (L) and then V'Length >= Size (L, T);

private

   type Posting_Map is record
      Amount, From, To, Currency, Fixed_Currency, Reference : Ada.Strings.Unbounded.Unbounded_String;
      Minor_Units : Minor_Digits := -1;
   end record;

   type Layout is new Ada.Finalization.Limited_Controlled with record
      Pkg : System.Address := System.Null_Address;
   end record;
   overriding procedure Finalize (L : in out Layout);

end Zxv.Eve;
