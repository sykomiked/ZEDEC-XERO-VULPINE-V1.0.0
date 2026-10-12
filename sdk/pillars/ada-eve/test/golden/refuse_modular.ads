--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--  must be refused: modular types wrap around; the pillar's integers do not.
package Refuse_Modular is
   type Byte is mod 2 ** 8;
end Refuse_Modular;
