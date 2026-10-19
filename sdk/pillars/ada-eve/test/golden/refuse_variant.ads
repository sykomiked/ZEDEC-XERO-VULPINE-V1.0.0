--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--  must be refused: a discriminated (variant) record.
package Refuse_Variant is
   type Kind is (A, B);
   type Item (K : Kind) is record
      case K is
         when A => X : Boolean;
         when B => null;
      end case;
   end record;
end Refuse_Variant;
