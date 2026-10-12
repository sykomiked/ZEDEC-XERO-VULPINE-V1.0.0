--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  must NOT compile: a Layout owns its C package: it is limited, so it cannot be copied.
--  expect: left hand of assignment must not be limited type
with Zxv.Eve; use Zxv.Eve;
procedure Layout_Assign is
   A, B : Layout;
begin
   A := B;
end Layout_Assign;
