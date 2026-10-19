--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  must compile: the correct counterpart of every case in this folder,
--  proving the compile-fail setup (flags, search path) works.
with Interfaces; use Interfaces;
with Zxv.Authority; use Zxv.Authority;
with Zxv.Decimals;
with Zxv.Eve;
with Zxv.Hexagrams; use Zxv.Hexagrams;
with Zxv.Logic; use Zxv.Logic;
with Zxv.Rationals; use Zxv.Rationals;
procedure OK_Control is
   package Naira is new Zxv.Decimals (Scale => 2, Digits_Count => 15);
   use type Naira.Amount;
   R : Rational := To_Rational (1, 2);
   H : constant Hexagram := From_Tag (59);
   T : constant Truth := To_Truth (Base_Of (H));
   N : constant Integer_64 := Numerator (R);
   A : constant Naira.Amount := 100;
   C : Capability;
   L : Zxv.Eve.Layout;
begin
   R := R + To_Rational (1, 2);
   if To_Boolean (T, On_Both => False, On_Neither => False) and N > 0 and A + 1 > 0 then
      null;
   end if;
   Release (C);
   L.Unload;
end OK_Control;
