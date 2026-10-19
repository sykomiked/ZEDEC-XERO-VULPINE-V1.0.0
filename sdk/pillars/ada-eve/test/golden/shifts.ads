--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  shifts.ads - enumeration subtypes, an enumeration index, a
--  three-dimensional array and nested records.
package Shifts is
   type Day is (Mon, Tue, Wed, Thu, Fri, Sat, Sun);
   subtype Weekday is Day range Mon .. Fri;
   subtype Weekend is Day range Sat .. Sun;
   type Hour is range 0 .. 23;
   type Minutes is range 0 .. 24 * 60;
   type Week is array (Day) of Minutes;
   subtype Work_Hour is Hour range 8 .. 17;
   type Rota is array (Weekday, Work_Hour, Boolean) of Boolean;
   type Slot is record
      On    : Weekday;
      Start : Work_Hour;
      Off   : Weekend;
   end record;
   type Plan is record
      First, Last : Slot;
      Total       : Week;
      Grid        : Rota;
   end record;
end Shifts;
