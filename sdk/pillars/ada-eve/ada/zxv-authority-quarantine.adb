--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
package body Zxv.Authority.Quarantine is

   use Interfaces;
   subtype Addr is System.Address;

   function C_Create (Cap : Addr; Capacity : Unsigned_32; Out_Log : access Addr) return Status_Code
     with Import, Convention => C, External_Name => "zxv_hexlog_create";
   function C_Append (Cap : Addr; Log : Addr; Tag, Reason : Unsigned_8; Submitter : Unsigned_32;
                      Subject : Addr; Seq : access Unsigned_64) return Status_Code
     with Import, Convention => C, External_Name => "zxv_hexlog_append";
   function C_Count (Log : Addr; N : access Unsigned_32) return Status_Code
     with Import, Convention => C, External_Name => "zxv_hexlog_count";
   function C_Head (Log : Addr; Out_Head : Addr) return Status_Code
     with Import, Convention => C, External_Name => "zxv_hexlog_head";
   function C_At (Log : Addr; Index : Unsigned_32; Seq : access Unsigned_64; Tag, Reason : access Unsigned_8;
                  Submitter : access Unsigned_32; Subject, Prev, Hash : Addr) return Status_Code
     with Import, Convention => C, External_Name => "zxv_hexlog_at";
   function C_Verify (Log : Addr) return Status_Code
     with Import, Convention => C, External_Name => "zxv_hexlog_verify";

   function Is_Open (L : Log) return Boolean is (L.N /= null);

   procedure Create (L : in out Log; Cap : Capability; Capacity : Positive) is
      H : aliased Addr := System.Null_Address;
   begin
      Check (C_Create (Handle_Of (Cap), Unsigned_32 (Capacity), H'Access), "Zxv.Authority.Quarantine.Create");
      --  The log keeps the capability's node (and through it the context)
      --  alive: R6, the log goes before its context.
      Retain (Node_Of (Cap));
      L.N := new Node'(Kind => Log_Node, Handle => H, Refs => 1, Parent => Node_Of (Cap));
   end Create;

   function Append (L : Log; Cap : Capability; Tag : Zxv.Hexagrams.Hexagram;
                    Reason : Interfaces.Unsigned_8; Submitter : Interfaces.Unsigned_32;
                    Subject : Digest) return Interfaces.Unsigned_64
   is
      S   : aliased Digest := Subject;
      Seq : aliased Unsigned_64 := 0;
   begin
      Check (C_Append (Handle_Of (Cap), L.N.Handle, Zxv.Hexagrams.Tag (Tag), Reason, Submitter, S'Address,
                       Seq'Access), "Zxv.Authority.Quarantine.Append");
      return Seq;
   end Append;

   function Count (L : Log) return Natural is
      N : aliased Unsigned_32 := 0;
   begin
      Check (C_Count (L.N.Handle, N'Access), "Zxv.Authority.Quarantine.Count");
      return Natural (N);
   end Count;

   function Head (L : Log) return Digest is
      D : aliased Digest := Zero_Digest;
   begin
      Check (C_Head (L.N.Handle, D'Address), "Zxv.Authority.Quarantine.Head");
      return D;
   end Head;

   function Record_At (L : Log; Index : Natural) return Record_View is
      Seq          : aliased Unsigned_64 := 0;
      Tag, Reason  : aliased Unsigned_8 := 0;
      Submitter    : aliased Unsigned_32 := 0;
      Sub, Pr, Hs  : aliased Digest := Zero_Digest;
   begin
      Check (C_At (L.N.Handle, Unsigned_32 (Index), Seq'Access, Tag'Access, Reason'Access, Submitter'Access,
                   Sub'Address, Pr'Address, Hs'Address), "Zxv.Authority.Quarantine.Record_At");
      if Tag > 63 then
         Check (E_Internal, "Zxv.Authority.Quarantine.Record_At", "a tag above 63");
      end if;
      return (Seq, Zxv.Hexagrams.From_Tag (Tag), Reason, Submitter, Sub, Pr, Hs);
   end Record_At;

   procedure Verify (L : Log) is
   begin
      Check (C_Verify (L.N.Handle), "Zxv.Authority.Quarantine.Verify");
   end Verify;

   function Hex (D : Digest) return String is
      K : constant String := "0123456789abcdef";
      S : String (1 .. 64);
   begin
      for I in D'Range loop
         S (2 * I - 1) := K (Natural (D (I) / 16) + 1);
         S (2 * I) := K (Natural (D (I) mod 16) + 1);
      end loop;
      return S;
   end Hex;

   overriding procedure Finalize (L : in out Log) is
   begin
      Release (L.N);
   end Finalize;

end Zxv.Authority.Quarantine;
