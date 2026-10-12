--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  broken.ads - must be REFUSED: money as a floating-point type. A
--  binary float cannot hold 0.10 exactly, so the pillar does not read it
--  as anything; it says where and why (make test checks this).
package Broken is
   type Money is digits 15;
   type Payment is record
      Amount : Money;
   end record;
end Broken;
