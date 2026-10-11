--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  must NOT compile: there is no conversion from Truth to Boolean (use To_Boolean with a policy).
--  expect: invalid conversion|illegal operand for .*conversion
with Zxv.Logic; use Zxv.Logic;
procedure Truth_To_Boolean is
   T : constant Truth := Neither;
   B : constant Boolean := Boolean (T);
begin
   null;
end Truth_To_Boolean;
