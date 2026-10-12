--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  Zxv.Authority - contexts and capabilities (zxv_ctx, zxv_cap).
--
--  Both are limited controlled types: they cannot be copied or assigned
--  (test/compile_fail), and when one goes out of scope its handle is
--  released and a capability's copy of its identity is wiped
--  (zxv_secure_zero). A capability keeps its parent (a context or the
--  capability it was attenuated from) alive, so the C handles are always
--  destroyed children first (ABI rule R6), in whatever order Ada
--  finalizes the objects.
--
--  A capability can only be narrowed (Attenuate), never widened. Gated
--  calls check it on every call. HONEST LIMIT: inside one process this is
--  least-privilege discipline, not a sandbox (ABI.md, R10).
with Interfaces;
with Zxv.Logic;
private with Ada.Finalization;
private with System;

package Zxv.Authority is

   ----------------------------------------------------------------------
   --  Contexts
   ----------------------------------------------------------------------

   type Context is tagged limited private;

   function Is_Open (Ctx : Context) return Boolean;

   procedure Create (Ctx : in out Context)
     with Pre => not Is_Open (Ctx), Post => Is_Open (Ctx);

   --  Poison the context on purpose (zxv_ctx_halt). From then on every call
   --  on it, or on anything made from it, is FATAL and stops the program.
   procedure Halt (Ctx : Context; Reason : String)
     with Pre => Is_Open (Ctx);

   function Is_Poisoned (Ctx : Context) return Boolean
     with Pre => Is_Open (Ctx);

   ----------------------------------------------------------------------
   --  Capabilities
   ----------------------------------------------------------------------

   type Right is (Ledger_Read, Ledger_Post, Crypto_Sign, Crypto_Kem, AI_Inference, Budget,
                  Phase_Control, Attest, Swarm, EVM);
   type Rights is array (Right) of Boolean;
   --  Rights combine with the predefined "or", "and" and "not" of Boolean
   --  arrays: Rights'(Ledger_Read | Ledger_Post => True, others => False).

   No_Rights  : constant Rights := (others => False);
   All_Rights : constant Rights := (others => True);

   --  The 21-octet (168-bit) identity a capability carries.
   type Identity is array (1 .. 21) of Interfaces.Unsigned_8;

   type Capability is tagged limited private;

   function Is_Held (Cap : Capability) return Boolean;

   --  A root capability of an open context; attestation starts True.
   procedure Create_Root (Cap        : in out Capability;
                          Ctx        : Context'Class;
                          Granted    : Rights;
                          Process_Id : Interfaces.Unsigned_32 := 0;
                          Id         : Identity := (others => 0))
     with Pre  => Is_Open (Ctx) and not Is_Held (Cap),
          Post => Is_Held (Cap);

   --  A child holding Parent's rights and Keep. Revoking the parent kills it.
   procedure Attenuate (Child : in out Capability; Parent : Capability; Keep : Rights)
     with Pre  => Is_Held (Parent) and not Is_Held (Child),
          Post => Is_Held (Child);

   --  Revoke this capability and, through it, every descendant.
   procedure Revoke (Cap : Capability)
     with Pre => Is_Held (Cap);

   procedure Set_Attestation (Cap : Capability; State : Zxv.Logic.Truth)
     with Pre => Is_Held (Cap);

   function Granted (Cap : Capability) return Rights
     with Pre => Is_Held (Cap);
   function Process_Id (Cap : Capability) return Interfaces.Unsigned_32
     with Pre => Is_Held (Cap);
   function Attestation (Cap : Capability) return Zxv.Logic.Truth
     with Pre => Is_Held (Cap);
   function Is_Revoked (Cap : Capability) return Boolean
     with Pre => Is_Held (Cap);

   --  True when Cap could make a call needing Needed (zxv_cap_check).
   function Allows (Cap : Capability; Needed : Rights) return Boolean
     with Pre => Is_Held (Cap);
   --  The same, raising Refused_Error (ZXV_E_CAP) when it could not.
   procedure Require (Cap : Capability; Needed : Rights)
     with Pre => Is_Held (Cap);

   --  Let go of the handle now (it is also released at finalization).
   procedure Release (Cap : in out Capability)
     with Post => not Is_Held (Cap);

private

   type Node_Kind is (Context_Node, Capability_Node, Log_Node);
   type Node;
   type Node_Access is access Node;
   type Node is record
      Kind   : Node_Kind;
      Handle : System.Address := System.Null_Address;
      Refs   : Natural := 1;
      Parent : Node_Access;
   end record;

   --  Reference counting under one lock, so handles may be finalized from
   --  any task. Release destroys the C handle when the last reference
   --  goes, then releases the parent.
   procedure Retain (N : Node_Access);
   procedure Release (N : in out Node_Access);

   function Handle_Of (Cap : Capability) return System.Address;
   function Node_Of (Cap : Capability) return Node_Access;

   type Context is new Ada.Finalization.Limited_Controlled with record
      N : Node_Access;
   end record;
   overriding procedure Finalize (Ctx : in out Context);

   type Capability is new Ada.Finalization.Limited_Controlled with record
      N  : Node_Access;
      Id : Identity := (others => 0);
   end record;
   overriding procedure Finalize (Cap : in out Capability);

end Zxv.Authority;
