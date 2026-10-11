--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  must NOT compile: a Capability is limited: it cannot be assigned (no copy of authority).
--  expect: left hand of assignment must not be limited type
with Zxv.Authority; use Zxv.Authority;
procedure Capability_Assign is
   A, B : Capability;
begin
   A := B;
end Capability_Assign;
