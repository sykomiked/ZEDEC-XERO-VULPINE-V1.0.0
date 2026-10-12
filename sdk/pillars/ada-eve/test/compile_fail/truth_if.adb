--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  must NOT compile: a four-valued Truth is not a Boolean condition.
--  expect: expected a boolean type
with Zxv.Logic; use Zxv.Logic;
procedure Truth_If is
   T : constant Truth := Both;
begin
   if T then
      null;
   end if;
end Truth_If;
