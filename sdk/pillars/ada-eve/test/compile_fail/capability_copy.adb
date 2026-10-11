--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  must NOT compile: a Capability cannot be copied into a new object either.
--  expect: initialization of limited object
with Zxv.Authority; use Zxv.Authority;
procedure Capability_Copy is
   A : Capability;
   B : Capability := A;
begin
   null;
end Capability_Copy;
