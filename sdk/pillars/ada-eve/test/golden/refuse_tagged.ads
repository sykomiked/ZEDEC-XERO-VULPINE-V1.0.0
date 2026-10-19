--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--  must be refused: a tagged record's layout is the compiler's.
package Refuse_Tagged is
   type Shape is tagged record
      Sides : Natural;
   end record;
end Refuse_Tagged;
