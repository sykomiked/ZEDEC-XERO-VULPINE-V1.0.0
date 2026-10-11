--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
with Ada.Unchecked_Deallocation;
with Interfaces.C;

package body Zxv.Authority is

   use Interfaces;

   function C_Ctx_Create (Out_Ctx : access System.Address) return Status_Code
     with Import, Convention => C, External_Name => "zxv_ctx_create";
   procedure C_Ctx_Destroy (Ctx : System.Address)
     with Import, Convention => C, External_Name => "zxv_ctx_destroy";
   function C_Ctx_Poisoned (Ctx : System.Address; P : access Integer_32) return Status_Code
     with Import, Convention => C, External_Name => "zxv_ctx_is_poisoned";
   function C_Ctx_Halt (Ctx : System.Address; Reason : System.Address) return Status_Code
     with Import, Convention => C, External_Name => "zxv_ctx_halt";
   function C_Cap_Create (Ctx : System.Address; Pid : Unsigned_32; Id : System.Address; Caps : Unsigned_64;
                          Out_Cap : access System.Address) return Status_Code
     with Import, Convention => C, External_Name => "zxv_cap_create";
   function C_Cap_Attenuate (Parent : System.Address; Keep : Unsigned_64; Out_Cap : access System.Address)
     return Status_Code
     with Import, Convention => C, External_Name => "zxv_cap_attenuate";
   function C_Cap_Revoke (Cap : System.Address) return Status_Code
     with Import, Convention => C, External_Name => "zxv_cap_revoke";
   function C_Cap_Set_Attestation (Cap : System.Address; State : Integer_32) return Status_Code
     with Import, Convention => C, External_Name => "zxv_cap_set_attestation";
   function C_Cap_Info (Cap : System.Address; Caps : access Unsigned_64; Pid : access Unsigned_32;
                        Att : access Integer_32; Revoked : access Integer_32) return Status_Code
     with Import, Convention => C, External_Name => "zxv_cap_info";
   function C_Cap_Check (Cap : System.Address; Needed : Unsigned_64) return Status_Code
     with Import, Convention => C, External_Name => "zxv_cap_check";
   procedure C_Cap_Destroy (Cap : System.Address)
     with Import, Convention => C, External_Name => "zxv_cap_destroy";
   procedure C_Log_Destroy (Log : System.Address)
     with Import, Convention => C, External_Name => "zxv_hexlog_destroy";
   function C_Secure_Zero (Buf : System.Address; Len : Interfaces.C.size_t) return Status_Code
     with Import, Convention => C, External_Name => "zxv_secure_zero";

   ----------------------------------------------------------------------
   --  Reference counts
   ----------------------------------------------------------------------

   protected Lock is
      procedure Increment (N : Node_Access);
      procedure Decrement (N : Node_Access; Left : out Natural);
   end Lock;

   protected body Lock is
      procedure Increment (N : Node_Access) is
      begin
         N.Refs := N.Refs + 1;
      end Increment;

      procedure Decrement (N : Node_Access; Left : out Natural) is
      begin
         N.Refs := N.Refs - 1;
         Left := N.Refs;
      end Decrement;
   end Lock;

   procedure Free is new Ada.Unchecked_Deallocation (Node, Node_Access);

   procedure Retain (N : Node_Access) is
   begin
      if N /= null then
         Lock.Increment (N);
      end if;
   end Retain;

   procedure Release (N : in out Node_Access) is
      Cur  : Node_Access := N;
      Next : Node_Access;
      Left : Natural;
   begin
      N := null;
      while Cur /= null loop
         Lock.Decrement (Cur, Left);
         exit when Left > 0;
         case Cur.Kind is
            when Context_Node    => C_Ctx_Destroy (Cur.Handle);
            when Capability_Node => C_Cap_Destroy (Cur.Handle);
            when Log_Node        => C_Log_Destroy (Cur.Handle);
         end case;
         Next := Cur.Parent;
         Free (Cur);
         Cur := Next;
      end loop;
   end Release;

   function To_Mask (R : Rights) return Unsigned_64 is
      M : Unsigned_64 := 0;
   begin
      for X in R'Range loop
         if R (X) then
            M := M or Shift_Left (1, Right'Pos (X));
         end if;
      end loop;
      return M;
   end To_Mask;

   function From_Mask (M : Unsigned_64) return Rights is
      R : Rights := No_Rights;
   begin
      for X in R'Range loop
         R (X) := (M and Shift_Left (1, Right'Pos (X))) /= 0;
      end loop;
      return R;
   end From_Mask;

   ----------------------------------------------------------------------
   --  Contexts
   ----------------------------------------------------------------------

   function Is_Open (Ctx : Context) return Boolean is (Ctx.N /= null);

   procedure Create (Ctx : in out Context) is
      H : aliased System.Address := System.Null_Address;
   begin
      Check (C_Ctx_Create (H'Access), "Zxv.Authority.Create");
      Ctx.N := new Node'(Kind => Context_Node, Handle => H, Refs => 1, Parent => null);
   end Create;

   procedure Halt (Ctx : Context; Reason : String) is
      R : constant String := C_Text (Reason);
   begin
      Check (C_Ctx_Halt (Ctx.N.Handle, R'Address), "Zxv.Authority.Halt");
   end Halt;

   function Is_Poisoned (Ctx : Context) return Boolean is
      P : aliased Integer_32 := 0;
   begin
      Check (C_Ctx_Poisoned (Ctx.N.Handle, P'Access), "Zxv.Authority.Is_Poisoned");
      return P /= 0;
   end Is_Poisoned;

   overriding procedure Finalize (Ctx : in out Context) is
   begin
      Release (Ctx.N);
   end Finalize;

   ----------------------------------------------------------------------
   --  Capabilities
   ----------------------------------------------------------------------

   function Is_Held (Cap : Capability) return Boolean is (Cap.N /= null);
   function Handle_Of (Cap : Capability) return System.Address is (Cap.N.Handle);
   function Node_Of (Cap : Capability) return Node_Access is (Cap.N);

   procedure Create_Root (Cap        : in out Capability;
                          Ctx        : Context'Class;
                          Granted    : Rights;
                          Process_Id : Interfaces.Unsigned_32 := 0;
                          Id         : Identity := (others => 0))
   is
      H : aliased System.Address := System.Null_Address;
   begin
      Cap.Id := Id;
      Check (C_Cap_Create (Ctx.N.Handle, Process_Id, Cap.Id'Address, To_Mask (Granted), H'Access),
             "Zxv.Authority.Create_Root");
      Retain (Ctx.N);
      Cap.N := new Node'(Kind => Capability_Node, Handle => H, Refs => 1, Parent => Ctx.N);
   end Create_Root;

   procedure Attenuate (Child : in out Capability; Parent : Capability; Keep : Rights) is
      H : aliased System.Address := System.Null_Address;
   begin
      Check (C_Cap_Attenuate (Parent.N.Handle, To_Mask (Keep), H'Access), "Zxv.Authority.Attenuate");
      Retain (Parent.N);
      Child.Id := Parent.Id;
      Child.N := new Node'(Kind => Capability_Node, Handle => H, Refs => 1, Parent => Parent.N);
   end Attenuate;

   procedure Revoke (Cap : Capability) is
   begin
      Check (C_Cap_Revoke (Cap.N.Handle), "Zxv.Authority.Revoke");
   end Revoke;

   procedure Set_Attestation (Cap : Capability; State : Zxv.Logic.Truth) is
   begin
      Check (C_Cap_Set_Attestation (Cap.N.Handle, Zxv.Logic.Code (State)), "Zxv.Authority.Set_Attestation");
   end Set_Attestation;

   procedure Info (Cap : Capability; Caps : out Unsigned_64; Pid : out Unsigned_32; Att : out Integer_32;
                   Revoked : out Boolean) is
      C : aliased Unsigned_64 := 0;
      P : aliased Unsigned_32 := 0;
      A, R : aliased Integer_32 := 0;
   begin
      Check (C_Cap_Info (Cap.N.Handle, C'Access, P'Access, A'Access, R'Access), "Zxv.Authority.Info");
      Caps := C;
      Pid := P;
      Att := A;
      Revoked := R /= 0;
   end Info;

   function Granted (Cap : Capability) return Rights is
      C : Unsigned_64;
      P : Unsigned_32;
      A : Integer_32;
      R : Boolean;
   begin
      Info (Cap, C, P, A, R);
      return From_Mask (C);
   end Granted;

   function Process_Id (Cap : Capability) return Interfaces.Unsigned_32 is
      C : Unsigned_64;
      P : Unsigned_32;
      A : Integer_32;
      R : Boolean;
   begin
      Info (Cap, C, P, A, R);
      return P;
   end Process_Id;

   function Attestation (Cap : Capability) return Zxv.Logic.Truth is
      C : Unsigned_64;
      P : Unsigned_32;
      A : Integer_32;
      R : Boolean;
   begin
      Info (Cap, C, P, A, R);
      if A not in 0 .. 3 then
         Check (E_Internal, "Zxv.Authority.Attestation", "attestation outside the four truth values");
      end if;
      return Zxv.Logic.From_Code (A);
   end Attestation;

   function Is_Revoked (Cap : Capability) return Boolean is
      C : Unsigned_64;
      P : Unsigned_32;
      A : Integer_32;
      R : Boolean;
   begin
      Info (Cap, C, P, A, R);
      return R;
   end Is_Revoked;

   function Allows (Cap : Capability; Needed : Rights) return Boolean is
      S : constant Status_Code := C_Cap_Check (Cap.N.Handle, To_Mask (Needed));
   begin
      if S = E_Cap then
         return False;
      end if;
      Check (S, "Zxv.Authority.Allows");
      return True;
   end Allows;

   procedure Require (Cap : Capability; Needed : Rights) is
   begin
      Check (C_Cap_Check (Cap.N.Handle, To_Mask (Needed)), "Zxv.Authority.Require");
   end Require;

   procedure Wipe (Cap : in out Capability) is
      S : constant Status_Code := C_Secure_Zero (Cap.Id'Address, Cap.Id'Length);
   begin
      if S /= Ok then
         Cap.Id := (others => 0);
      end if;
   end Wipe;

   procedure Release (Cap : in out Capability) is
   begin
      Wipe (Cap);
      Release (Cap.N);
   end Release;

   overriding procedure Finalize (Cap : in out Capability) is
   begin
      Wipe (Cap);
      Release (Cap.N);
   end Finalize;

end Zxv.Authority;
