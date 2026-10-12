--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
package body Zxv.Fixed with SPARK_Mode is

   function Rescale (D : Decimal; To : Scale_Range) return Decimal is
   begin
      if To >= D.Scale then
         return (Value => D.Value * Pow10 (To - D.Scale), Scale => To);
      else
         return (Value => D.Value / Pow10 (D.Scale - To), Scale => To);
      end if;
   end Rescale;

   function Add (A, B : Decimal) return Decimal is ((Value => A.Value + B.Value, Scale => A.Scale));
   function Subtract (A, B : Decimal) return Decimal is ((Value => A.Value - B.Value, Scale => A.Scale));

   function Compare (A, B : Decimal) return Integer is
      --  Compare the whole parts first, then the fractions at the larger
      --  scale; every step stays inside 18 digits.
      PA : constant Int64 := Pow10 (A.Scale);
      PB : constant Int64 := Pow10 (B.Scale);
      WA : constant Int64 := A.Value / PA;
      WB : constant Int64 := B.Value / PB;
      FA : Int64 := A.Value rem PA;
      FB : Int64 := B.Value rem PB;
   begin
      if WA /= WB then
         return (if WA < WB then -1 else 1);
      end if;
      if A.Scale < B.Scale then
         FA := FA * Pow10 (B.Scale - A.Scale);
      else
         FB := FB * Pow10 (A.Scale - B.Scale);
      end if;
      if FA = FB then
         return 0;
      end if;
      return (if FA < FB then -1 else 1);
   end Compare;

   function Image (D : Decimal) return String is
      Buf : String (1 .. Image_Max) := (others => '0');
      K   : Natural := Image_Max;
      U   : Int64 := abs D.Value;
   begin
      --  At most 19 digit positions (18 digits, or "0." and 18 decimals),
      --  one point and one sign: Image_Max = 21.
      for N in 0 .. 18 loop
         pragma Loop_Invariant (K = Image_Max - N - (if D.Scale > 0 and N > D.Scale then 1 else 0));
         pragma Loop_Invariant (U >= 0);
         if D.Scale > 0 and N = D.Scale then
            Buf (K) := '.';
            K := K - 1;
         end if;
         Buf (K) := Character'Val (Character'Pos ('0') + Natural (U rem 10));
         K := K - 1;
         U := U / 10;
         exit when U = 0 and N >= D.Scale;
      end loop;
      if D.Value < 0 then
         Buf (K) := '-';
         K := K - 1;
      end if;
      declare
         Len    : constant Natural := Image_Max - K;
         Result : String (1 .. Len);
      begin
         for I in 1 .. Len loop
            Result (I) := Buf (K + I);
         end loop;
         return Result;
      end;
   end Image;

   procedure Value (Text : String; Scale : Scale_Range; Result : out Decimal; Ok : out Boolean) is
      Acc      : Int64 := 0;
      Neg      : Boolean := False;
      Seen     : Boolean := False;   --  a digit was read
      Decimals : Natural := 0;   --  digits after the point accumulated
      Point    : Boolean := False;
      I        : Natural;
   begin
      Result := (Value => 0, Scale => Scale);
      Ok := False;
      if Text'Length = 0 or Text'Last = Natural'Last then
         return;
      end if;
      I := Text'First;
      if Text (I) = '-' or Text (I) = '+' then
         Neg := Text (I) = '-';
         I := I + 1;
      end if;
      if I > Text'Last then
         return;
      end if;
      while I <= Text'Last loop
         pragma Loop_Invariant (Acc in 0 .. Max_Units);
         pragma Loop_Invariant (Decimals <= Scale);
         pragma Loop_Invariant (I in Text'Range);
         pragma Loop_Variant (Increases => I);
         declare
            C : constant Character := Text (I);
         begin
            if C = '.' then
               if Point or not Seen then
                  return;
               end if;
               Point := True;
            elsif C in '0' .. '9' then
               declare
                  Digit : constant Int64 := Int64 (Character'Pos (C) - Character'Pos ('0'));
               begin
                  if Point and Decimals = Scale then
                     --  a decimal beyond the scale: only a zero is exact
                     if Digit /= 0 then
                        return;
                     end if;
                  else
                     if Acc > (Max_Units - Digit) / 10 then
                        return;
                     end if;
                     Acc := Acc * 10 + Digit;
                     if Point then
                        Decimals := Decimals + 1;
                     end if;
                  end if;
                  Seen := True;
               end;
            else
               return;
            end if;
         end;
         exit when I = Text'Last;
         I := I + 1;
      end loop;
      if not Seen or (Point and then Text (Text'Last) = '.') then
         return;
      end if;
      --  scale up the remaining decimals
      while Decimals < Scale loop
         pragma Loop_Invariant (Acc in 0 .. Max_Units);
         pragma Loop_Variant (Increases => Decimals);
         if Acc > Max_Units / 10 then
            return;
         end if;
         Acc := Acc * 10;
         Decimals := Decimals + 1;
      end loop;
      Result := (Value => (if Neg then -Acc else Acc), Scale => Scale);
      Ok := True;
   end Value;

end Zxv.Fixed;
