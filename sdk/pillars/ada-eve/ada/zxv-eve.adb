--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
with Ada.Directories;
with Ada.Streams.Stream_IO;
with Interfaces.C;
with Interfaces.C.Strings;
with Zxv.Authority.Interop;

package body Zxv.Eve is

   use Interfaces;
   use type Interfaces.C.size_t;
   use type System.Address;
   use type Interfaces.C.char;
   use type Ada.Directories.File_Size;
   use Ada.Strings.Unbounded;

   subtype Addr is System.Address;
   subtype Size_T is Interfaces.C.size_t;

   type C_Diag is record
      Line, Col : Unsigned_32;
      Offset    : Unsigned_64;
      Msg       : Interfaces.C.char_array (0 .. 255);
   end record
     with Convention => C;

   type C_Type is record
      Name       : Interfaces.C.char_array (0 .. 63);
      Kind       : Integer_32;
      Is_Subtype : Integer_32;
      Base       : Unsigned_32;
      Lo, Hi     : Integer_64;
      Dig, Scale : Unsigned_32;
      Count      : Unsigned_32;
      Elem       : Unsigned_32;
      Dims       : Unsigned_32;
      Size       : Unsigned_64;
   end record
     with Convention => C;

   type C_Dim is record
      Index  : Unsigned_32;
      Lo, Hi : Integer_64;
   end record
     with Convention => C;

   type C_Component is record
      Name   : Interfaces.C.char_array (0 .. 63);
      Of_T   : Unsigned_32;
      Offset : Unsigned_64;
   end record
     with Convention => C;

   type C_Map is record
      Amount, From, To, Currency, Fixed_Currency, Reference : Interfaces.C.Strings.chars_ptr;
      Minor_Units : Integer_32;
   end record
     with Convention => C;

   type C_Posting is record
      Amount_Minor : Integer_64;
      From, To     : Unsigned_32;
      Currency     : Interfaces.C.char_array (0 .. 7);
      Reference    : Interfaces.C.char_array (0 .. 35);
      Entry_Id     : Unsigned_64;
   end record
     with Convention => C;

   function C_Ada_Parse (Text : Addr; Len : Size_T; Pkg : access Addr; D : access C_Diag) return Status_Code
     with Import, Convention => C, External_Name => "ze_ada_parse";
   function C_Eve_Parse (Text : Addr; Len : Size_T; Pkg : access Addr; D : access C_Diag) return Status_Code
     with Import, Convention => C, External_Name => "ze_eve_parse";
   procedure C_Destroy (Pkg : Addr)
     with Import, Convention => C, External_Name => "ze_pkg_destroy";
   function C_Eve (Pkg : Addr; Buf : Addr; Cap : Size_T; Len : access Size_T) return Status_Code
     with Import, Convention => C, External_Name => "ze_pkg_eve";
   function C_Ada (Pkg : Addr; Buf : Addr; Cap : Size_T; Len : access Size_T) return Status_Code
     with Import, Convention => C, External_Name => "ze_pkg_ada";
   function C_Name (Pkg : Addr; Buf : Addr; Cap : Size_T; Len : access Size_T) return Status_Code
     with Import, Convention => C, External_Name => "ze_pkg_name";
   function C_Digest (Pkg : Addr; D : out Digest) return Status_Code
     with Import, Convention => C, External_Name => "ze_pkg_digest";
   function C_Same (A, B : Addr; Same : access Integer_32) return Status_Code
     with Import, Convention => C, External_Name => "ze_pkg_same";
   function C_Type_Count (Pkg : Addr; N : access Unsigned_32) return Status_Code
     with Import, Convention => C, External_Name => "ze_type_count";
   function C_Type_At (Pkg : Addr; T : Unsigned_32; Info : access C_Type) return Status_Code
     with Import, Convention => C, External_Name => "ze_type_at";
   function C_Type_Find (Pkg : Addr; Name : Addr; T : access Unsigned_32) return Status_Code
     with Import, Convention => C, External_Name => "ze_type_find";
   function C_Array_Dim (Pkg : Addr; T, D : Unsigned_32; Info : access C_Dim) return Status_Code
     with Import, Convention => C, External_Name => "ze_array_dim";
   function C_Component_At (Pkg : Addr; T, I : Unsigned_32; Info : access C_Component) return Status_Code
     with Import, Convention => C, External_Name => "ze_component_at";
   function C_Enum_Literal (Pkg : Addr; T, Pos : Unsigned_32; Buf : Addr; Cap : Size_T;
                            Len : access Size_T) return Status_Code
     with Import, Convention => C, External_Name => "ze_enum_literal";

   function C_Get_Decimal (Pkg : Addr; T : Unsigned_32; V : Addr; Len : Size_T; Path : Addr;
                           Units : access Integer_64; Scale : access Unsigned_32) return Status_Code
     with Import, Convention => C, External_Name => "ze_get_decimal";
   function C_Get_Rat (Pkg : Addr; T : Unsigned_32; V : Addr; Len : Size_T; Path : Addr;
                       N, D : access Integer_64) return Status_Code
     with Import, Convention => C, External_Name => "ze_get_rat";
   function C_Get_Enum (Pkg : Addr; T : Unsigned_32; V : Addr; Len : Size_T; Path : Addr;
                        Pos : access Unsigned_32) return Status_Code
     with Import, Convention => C, External_Name => "ze_get_enum";
   function C_Get_Value (Pkg : Addr; T : Unsigned_32; V : Addr; Len : Size_T; Path : Addr;
                         Buf : Addr; Cap : Size_T; Out_Len : access Size_T) return Status_Code
     with Import, Convention => C, External_Name => "ze_get_value";
   function C_Put_Decimal (Pkg : Addr; T : Unsigned_32; V : Addr; Len : Size_T; Path : Addr;
                           Units : Integer_64; Scale : Unsigned_32) return Status_Code
     with Import, Convention => C, External_Name => "ze_put_decimal";
   function C_Put_Rat (Pkg : Addr; T : Unsigned_32; V : Addr; Len : Size_T; Path : Addr;
                       N, D : Integer_64) return Status_Code
     with Import, Convention => C, External_Name => "ze_put_rat";
   function C_Put_Enum (Pkg : Addr; T : Unsigned_32; V : Addr; Len : Size_T; Path : Addr;
                        Pos : Unsigned_32) return Status_Code
     with Import, Convention => C, External_Name => "ze_put_enum";
   function C_Put_Value (Pkg : Addr; T : Unsigned_32; V : Addr; Len : Size_T; Path : Addr;
                         Text : Addr; Text_Len : Size_T) return Status_Code
     with Import, Convention => C, External_Name => "ze_put_value";
   function C_Init (Pkg : Addr; T : Unsigned_32; V : Addr; Len : Size_T) return Status_Code
     with Import, Convention => C, External_Name => "ze_value_init";
   function C_Check (Pkg : Addr; T : Unsigned_32; V : Addr; Len : Size_T) return Status_Code
     with Import, Convention => C, External_Name => "ze_value_check";
   function C_Posting_Read (Pkg : Addr; T : Unsigned_32; M : access constant C_Map; V : Addr; Len : Size_T;
                            P : access C_Posting) return Status_Code
     with Import, Convention => C, External_Name => "ze_posting_read";
   function C_Post (Cap : Addr; Pkg : Addr; T : Unsigned_32; M : access constant C_Map; V : Addr;
                    Len : Size_T; P : access C_Posting) return Status_Code
     with Import, Convention => C, External_Name => "ze_post";

   --  The address of V's first byte, or null for an empty value.
   function Data (V : Bytes) return Addr is (if V'Length = 0 then System.Null_Address else V (V'First)'Address);

   function C_Name_Of (A : Interfaces.C.char_array) return String is
      N : Natural := 0;
   begin
      while N <= Natural (A'Last) and then A (Size_T (N)) /= Interfaces.C.nul loop
         N := N + 1;
      end loop;
      return R : String (1 .. N) do
         for I in R'Range loop
            R (I) := Character (A (Size_T (I - 1)));
         end loop;
      end return;
   end C_Name_Of;

   ----------------------------------------------------------------------
   --  Parsing
   ----------------------------------------------------------------------

   function Is_Loaded (L : Layout) return Boolean is (L.Pkg /= System.Null_Address);

   procedure Try_Parse (L : in out Layout; Text : String; Form : Source_Form;
                        Status : out Status_Code; Where : out Diagnostic) is
      P : aliased Addr := System.Null_Address;
      D : aliased C_Diag := (0, 0, 0, (others => Interfaces.C.nul));
      T : constant Addr := (if Text'Length = 0 then C_Text ("")'Address else Text (Text'First)'Address);
   begin
      Where := (others => <>);
      Status := (case Form is
                    when Ada_Spec => C_Ada_Parse (T, Text'Length, P'Access, D'Access),
                    when Eve_Text => C_Eve_Parse (T, Text'Length, P'Access, D'Access));
      if Status = Ok then
         L.Pkg := P;
      else
         declare
            M : constant String := C_Name_Of (D.Msg);
         begin
            Where.Line := Natural (D.Line);
            Where.Column := Natural (D.Col);
            Where.Length := Natural'Min (M'Length, Where.Message'Length);
            Where.Message (1 .. Where.Length) := M (M'First .. M'First + Where.Length - 1);
         end;
      end if;
   end Try_Parse;

   procedure Parse (L : in out Layout; Text : String; Form : Source_Form; What : String) is
      Status : Status_Code;
      Where  : Diagnostic;
   begin
      Try_Parse (L, Text, Form, Status, Where);
      Check (Status, What, Where.Message (1 .. Where.Length));
   end Parse;

   procedure Parse_Ada (L : in out Layout; Text : String) is
   begin
      Parse (L, Text, Ada_Spec, "Zxv.Eve.Parse_Ada");
   end Parse_Ada;

   procedure Parse_Eve (L : in out Layout; Text : String) is
   begin
      Parse (L, Text, Eve_Text, "Zxv.Eve.Parse_Eve");
   end Parse_Eve;

   procedure Load (L : in out Layout; Path : String) is
      use Ada.Streams.Stream_IO;
      F : File_Type;
      N : Ada.Directories.File_Size;
   begin
      begin
         N := Ada.Directories.Size (Path);
      exception
         when others =>
            Check (E_IO, "Zxv.Eve.Load", "cannot read " & Path);
      end;
      if N > 1024 * 1024 then
         Check (ZE_E_Limit, "Zxv.Eve.Load", Path & " is longer than 1 MiB");
      end if;
      declare
         Text : String (1 .. Natural (N));
      begin
         begin
            Open (F, In_File, Path);
            String'Read (Stream (F), Text);
            Close (F);
         exception
            when others =>
               if Is_Open (F) then
                  Close (F);
               end if;
               Check (E_IO, "Zxv.Eve.Load", "cannot read " & Path);
         end;
         Parse (L, Text, (if Path'Length > 4 and then Path (Path'Last - 3 .. Path'Last) = ".ads"
                          then Ada_Spec else Eve_Text),
                "Zxv.Eve.Load " & Path);
      end;
   end Load;

   procedure Unload (L : in out Layout) is
   begin
      if L.Pkg /= System.Null_Address then
         C_Destroy (L.Pkg);
         L.Pkg := System.Null_Address;
      end if;
   end Unload;

   overriding procedure Finalize (L : in out Layout) is
   begin
      Unload (L);
   end Finalize;

   type Printer is access function (Pkg : Addr; Buf : Addr; Cap : Size_T; Len : access Size_T)
     return Status_Code with Convention => C;

   function Print (L : Layout; P : Printer; What : String) return String is
      Need : aliased Size_T := 0;
      St   : Status_Code := P (L.Pkg, System.Null_Address, 0, Need'Access);
   begin
      if St /= E_Buffer and St /= Ok then
         Check (St, What);
      end if;
      declare
         Buf : String (1 .. Natural (Need) + 1);
      begin
         St := P (L.Pkg, Buf'Address, Buf'Length, Need'Access);
         Check (St, What);
         return Buf (1 .. Natural (Need));
      end;
   end Print;

   function Name (L : Layout) return String is (Print (L, C_Name'Access, "Zxv.Eve.Name"));
   function Canonical (L : Layout) return String is (Print (L, C_Eve'Access, "Zxv.Eve.Canonical"));
   function As_Ada (L : Layout) return String is (Print (L, C_Ada'Access, "Zxv.Eve.As_Ada"));

   function Package_Digest (L : Layout) return Digest is
      D : Digest := (others => 0);
   begin
      Check (C_Digest (L.Pkg, D), "Zxv.Eve.Package_Digest");
      return D;
   end Package_Digest;

   function Hex (D : Digest) return String is
      H : constant String := "0123456789abcdef";
   begin
      return R : String (1 .. 64) do
         for I in D'Range loop
            R (2 * I - 1) := H (Natural (Shift_Right (D (I), 4)) + 1);
            R (2 * I) := H (Natural (D (I) and 15) + 1);
         end loop;
      end return;
   end Hex;

   function Same (A, B : Layout) return Boolean is
      S : aliased Integer_32 := 0;
   begin
      Check (C_Same (A.Pkg, B.Pkg, S'Access), "Zxv.Eve.Same");
      return S = 1;
   end Same;

   ----------------------------------------------------------------------
   --  Types
   ----------------------------------------------------------------------

   function Type_Count (L : Layout) return Natural is
      N : aliased Unsigned_32 := 0;
   begin
      Check (C_Type_Count (L.Pkg, N'Access), "Zxv.Eve.Type_Count");
      return Natural (N);
   end Type_Count;

   function Find (L : Layout; Type_Name : String) return Type_Ref is
      N : constant String := C_Text (Type_Name);
      T : aliased Unsigned_32 := 0;
      S : constant Status_Code := C_Type_Find (L.Pkg, N'Address, T'Access);
   begin
      Check (S, "Zxv.Eve.Find", (if S = Ok then "" else "no type named " & Type_Name));
      return Type_Ref (T);
   end Find;

   function Info (L : Layout; T : Type_Ref) return Type_Info is
      C : aliased C_Type;
   begin
      Check (C_Type_At (L.Pkg, Unsigned_32 (T), C'Access), "Zxv.Eve.Info");
      declare
         N : constant String := C_Name_Of (C.Name);
         R : Type_Info :=
           (Name => (others => ' '), Name_Length => N'Length,
            Of_Kind => Kind'Val (C.Kind - 1), Is_Subtype => C.Is_Subtype /= 0,
            Base => Type_Ref (C.Base), Lo => C.Lo, Hi => C.Hi,
            Digits_Count => Natural (C.Dig), Scale => Natural (C.Scale), Count => Natural (C.Count),
            Element => Type_Ref (C.Elem), Dimensions => Natural (C.Dims), Size => Natural (C.Size));
      begin
         R.Name (1 .. N'Length) := N;
         return R;
      end;
   end Info;

   function Size (L : Layout; T : Type_Ref) return Natural is (Info (L, T).Size);

   function Dimension (L : Layout; T : Type_Ref; D : Positive) return Dimension_Info is
      C : aliased C_Dim;
   begin
      Check (C_Array_Dim (L.Pkg, Unsigned_32 (T), Unsigned_32 (D - 1), C'Access), "Zxv.Eve.Dimension");
      return (Type_Ref (C.Index), C.Lo, C.Hi);
   end Dimension;

   function Component (L : Layout; T : Type_Ref; Index : Positive) return Component_Info is
      C : aliased C_Component;
   begin
      Check (C_Component_At (L.Pkg, Unsigned_32 (T), Unsigned_32 (Index - 1), C'Access), "Zxv.Eve.Component");
      declare
         N : constant String := C_Name_Of (C.Name);
         R : Component_Info := ((others => ' '), N'Length, Type_Ref (C.Of_T), Natural (C.Offset));
      begin
         R.Name (1 .. N'Length) := N;
         return R;
      end;
   end Component;

   function Literal (L : Layout; T : Type_Ref; Position : Natural) return String is
      Buf : String (1 .. 64);
      N   : aliased Size_T := 0;
   begin
      Check (C_Enum_Literal (L.Pkg, Unsigned_32 (T), Unsigned_32 (Position), Buf'Address, Buf'Length, N'Access),
             "Zxv.Eve.Literal");
      return Buf (1 .. Natural (N));
   end Literal;

   ----------------------------------------------------------------------
   --  Values
   ----------------------------------------------------------------------

   function New_Value (L : Layout; T : Type_Ref) return Bytes is
      V : Bytes (1 .. Size (L, T));
   begin
      V := (others => 0);  --  then filled through its address by ze_value_init
      Check (C_Init (L.Pkg, Unsigned_32 (T), Data (V), V'Length), "Zxv.Eve.New_Value");
      return V;
   end New_Value;

   procedure Check_Value (L : Layout; T : Type_Ref; V : Bytes) is
   begin
      Check (C_Check (L.Pkg, Unsigned_32 (T), Data (V), V'Length), "Zxv.Eve.Check_Value");
   end Check_Value;

   function Is_Valid (L : Layout; T : Type_Ref; V : Bytes) return Boolean is
      S : constant Status_Code := C_Check (L.Pkg, Unsigned_32 (T), Data (V), V'Length);
   begin
      if S = ZE_E_Data then
         return False;
      end if;
      Check (S, "Zxv.Eve.Is_Valid");
      return True;
   end Is_Valid;

   procedure Get_Units (L : Layout; T : Type_Ref; V : Bytes; Path : String; What : String;
                        Units : out Integer_64; Scale : out Natural) is
      P  : constant String := C_Text (Path);
      U  : aliased Integer_64 := 0;
      Sc : aliased Unsigned_32 := 0;
   begin
      Check (C_Get_Decimal (L.Pkg, Unsigned_32 (T), Data (V), V'Length, P'Address, U'Access, Sc'Access), What);
      Units := U;
      Scale := Natural (Sc);
   end Get_Units;

   function Get_Decimal (L : Layout; T : Type_Ref; V : Bytes; Path : String) return Zxv.Fixed.Decimal is
      U  : Integer_64;
      Sc : Natural;
   begin
      Get_Units (L, T, V, Path, "Zxv.Eve.Get_Decimal", U, Sc);
      if U not in Zxv.Fixed.Units then
         Check (E_Range, "Zxv.Eve.Get_Decimal", Path & " holds" & Integer_64'Image (U)
                & ", outside the 18 digits of Zxv.Fixed.Decimal (use Get_Integer)");
      end if;
      return (Value => U, Scale => Sc);
   end Get_Decimal;

   function Get_Integer (L : Layout; T : Type_Ref; V : Bytes; Path : String) return Integer_64 is
      U  : Integer_64;
      Sc : Natural;
   begin
      Get_Units (L, T, V, Path, "Zxv.Eve.Get_Integer", U, Sc);
      if Sc /= 0 then
         Check (E_Arg, "Zxv.Eve.Get_Integer", Path & " is a decimal (use Get_Decimal)");
      end if;
      return U;
   end Get_Integer;

   function Get_Rational (L : Layout; T : Type_Ref; V : Bytes; Path : String) return Zxv.Rationals.Rational is
      P    : constant String := C_Text (Path);
      N, D : aliased Integer_64 := 0;
   begin
      Check (C_Get_Rat (L.Pkg, Unsigned_32 (T), Data (V), V'Length, P'Address, N'Access, D'Access),
             "Zxv.Eve.Get_Rational");
      return Zxv.Rationals.To_Rational (N, D);
   end Get_Rational;

   function Get_Position (L : Layout; T : Type_Ref; V : Bytes; Path : String) return Natural is
      P   : constant String := C_Text (Path);
      Pos : aliased Unsigned_32 := 0;
   begin
      Check (C_Get_Enum (L.Pkg, Unsigned_32 (T), Data (V), V'Length, P'Address, Pos'Access),
             "Zxv.Eve.Get_Position");
      return Natural (Pos);
   end Get_Position;

   function Get_Text (L : Layout; T : Type_Ref; V : Bytes; Path : String) return String is
      P    : constant String := C_Text (Path);
      Need : aliased Size_T := 0;
      St   : constant Status_Code :=
        C_Get_Value (L.Pkg, Unsigned_32 (T), Data (V), V'Length, P'Address, System.Null_Address, 0, Need'Access);
   begin
      if St /= E_Buffer and St /= Ok then
         Check (St, "Zxv.Eve.Get_Text");
      end if;
      declare
         Buf : String (1 .. Natural (Need) + 1);
      begin
         Check (C_Get_Value (L.Pkg, Unsigned_32 (T), Data (V), V'Length, P'Address, Buf'Address, Buf'Length,
                             Need'Access), "Zxv.Eve.Get_Text");
         return Buf (1 .. Natural (Need));
      end;
   end Get_Text;

   procedure Put_Decimal (L : Layout; T : Type_Ref; V : in out Bytes; Path : String;
                          Value : Zxv.Fixed.Decimal) is
      P : constant String := C_Text (Path);
   begin
      Check (C_Put_Decimal (L.Pkg, Unsigned_32 (T), Data (V), V'Length, P'Address, Value.Value,
                            Unsigned_32 (Value.Scale)), "Zxv.Eve.Put_Decimal");
   end Put_Decimal;

   procedure Put_Integer (L : Layout; T : Type_Ref; V : in out Bytes; Path : String;
                          Value : Integer_64) is
      P : constant String := C_Text (Path);
   begin
      Check (C_Put_Decimal (L.Pkg, Unsigned_32 (T), Data (V), V'Length, P'Address, Value, 0),
             "Zxv.Eve.Put_Integer");
   end Put_Integer;

   procedure Put_Rational (L : Layout; T : Type_Ref; V : in out Bytes; Path : String;
                           Value : Zxv.Rationals.Rational) is
      P : constant String := C_Text (Path);
   begin
      Check (C_Put_Rat (L.Pkg, Unsigned_32 (T), Data (V), V'Length, P'Address,
                        Zxv.Rationals.Numerator (Value), Zxv.Rationals.Denominator (Value)),
             "Zxv.Eve.Put_Rational");
   end Put_Rational;

   procedure Put_Position (L : Layout; T : Type_Ref; V : in out Bytes; Path : String;
                           Position : Natural) is
      P : constant String := C_Text (Path);
   begin
      Check (C_Put_Enum (L.Pkg, Unsigned_32 (T), Data (V), V'Length, P'Address, Unsigned_32 (Position)),
             "Zxv.Eve.Put_Position");
   end Put_Position;

   procedure Put_Text (L : Layout; T : Type_Ref; V : in out Bytes; Path : String; Text : String) is
      P : constant String := C_Text (Path);
      X : constant String := C_Text (Text);
   begin
      Check (C_Put_Value (L.Pkg, Unsigned_32 (T), Data (V), V'Length, P'Address, X'Address, Text'Length),
             "Zxv.Eve.Put_Text");
   end Put_Text;

   ----------------------------------------------------------------------
   --  Postings
   ----------------------------------------------------------------------

   function Map (Amount, From, To, Reference : String;
                 Currency       : String := "";
                 Fixed_Currency : String := "";
                 Minor_Units    : Minor_Digits := -1) return Posting_Map is
     ((To_Unbounded_String (Amount), To_Unbounded_String (From), To_Unbounded_String (To),
       To_Unbounded_String (Currency), To_Unbounded_String (Fixed_Currency),
       To_Unbounded_String (Reference), Minor_Units));

   --  The C map: NUL-terminated copies that live until Free_Map.
   function To_C (M : Posting_Map) return C_Map is
      use Interfaces.C.Strings;
      function S (U : Unbounded_String) return chars_ptr is
        (if Length (U) = 0 then Null_Ptr else New_String (To_String (U)));
   begin
      return (S (M.Amount), S (M.From), S (M.To), S (M.Currency), S (M.Fixed_Currency), S (M.Reference),
              Integer_32 (M.Minor_Units));
   end To_C;

   procedure Free_Map (C : in out C_Map) is
      use Interfaces.C.Strings;
   begin
      Free (C.Amount);
      Free (C.From);
      Free (C.To);
      Free (C.Currency);
      Free (C.Fixed_Currency);
      Free (C.Reference);
   end Free_Map;

   function From_C (C : C_Posting) return Posting is
      Ccy : constant String := C_Name_Of (C.Currency);
      Ref : constant String := C_Name_Of (C.Reference);
      R   : Posting := (Amount_Minor => C.Amount_Minor,
                        From => Zxv.Authority.Ledger.Account_Id (C.From),
                        To => Zxv.Authority.Ledger.Account_Id (C.To),
                        Currency => (others => ' '), Currency_Length => Ccy'Length,
                        Reference => (others => ' '), Reference_Length => Ref'Length,
                        Entry_Ref => Zxv.Authority.Ledger.Entry_Id (C.Entry_Id));
   begin
      R.Currency (1 .. Ccy'Length) := Ccy;
      R.Reference (1 .. Ref'Length) := Ref;
      return R;
   end From_C;

   Empty_Posting : constant C_Posting :=
     (0, 0, 0, (others => Interfaces.C.nul), (others => Interfaces.C.nul), 0);

   function Read_Posting (L : Layout; T : Type_Ref; Map : Posting_Map; V : Bytes) return Posting is
      M  : aliased C_Map := To_C (Map);
      P  : aliased C_Posting := Empty_Posting;
      St : constant Status_Code := C_Posting_Read (L.Pkg, Unsigned_32 (T), M'Access, Data (V), V'Length, P'Access);
   begin
      Free_Map (M);
      Check (St, "Zxv.Eve.Read_Posting");
      return From_C (P);
   end Read_Posting;

   procedure Try_Post (Cap : Zxv.Authority.Capability; L : Layout; T : Type_Ref; Map : Posting_Map;
                       V : Bytes; Result : out Posting; Status : out Status_Code) is
      M : aliased C_Map := To_C (Map);
      P : aliased C_Posting := Empty_Posting;
   begin
      Status := C_Post (Zxv.Authority.Interop.Capability_Handle (Cap), L.Pkg, Unsigned_32 (T), M'Access,
                        Data (V), V'Length, P'Access);
      Free_Map (M);
      Result := From_C (P);
      if Class_Of (Status) = Class_Fatal then
         Fatal_Stop (Status, "Zxv.Eve.Try_Post", Last_Error);
      end if;
   end Try_Post;

   function Post (Cap : Zxv.Authority.Capability; L : Layout; T : Type_Ref; Map : Posting_Map;
                  V : Bytes) return Posting is
      Result : Posting;
      Status : Status_Code;
   begin
      Try_Post (Cap, L, T, Map, V, Result, Status);
      Check (Status, "Zxv.Eve.Post");
      return Result;
   end Post;

end Zxv.Eve;
