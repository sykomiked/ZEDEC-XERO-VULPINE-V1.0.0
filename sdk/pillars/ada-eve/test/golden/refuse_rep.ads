--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--  must be refused: a representation clause would make the layout the
--  compiler's again.
package Refuse_Rep is
   type Code is range 0 .. 9;
   for Code'Size use 8;
end Refuse_Rep;
