--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  Zxv.Authority.Quarantine - the append-only, hash-chained log of
--  forbidden work (zxv_hexlog, ABI 1.3). Creating and appending need the
--  Attest right; reading needs none. Only a forbidden tag can be
--  quarantined. A full log refuses with ZXV_E_CAPACITY, and the caller
--  must then halt: forbidden work it cannot record must not continue.
--  The chain hash is SHA-256, as in the kernel.
with Interfaces;
with Zxv.Hexagrams;
private with Ada.Finalization;

package Zxv.Authority.Quarantine is

   use type Zxv.Hexagrams.Polarity;

   type Digest is array (1 .. 32) of Interfaces.Unsigned_8;
   Zero_Digest : constant Digest := (others => 0);

   type Log is tagged limited private;

   function Is_Open (L : Log) return Boolean;

   procedure Create (L : in out Log; Cap : Capability; Capacity : Positive)
     with Pre => not Is_Open (L) and Is_Held (Cap) and Capacity <= 65_536, Post => Is_Open (L);

   --  Quarantine one record; returns its sequence number.
   function Append (L : Log; Cap : Capability; Tag : Zxv.Hexagrams.Hexagram;
                    Reason : Interfaces.Unsigned_8; Submitter : Interfaces.Unsigned_32;
                    Subject : Digest) return Interfaces.Unsigned_64
     with Pre => Is_Open (L) and Is_Held (Cap)
                 and Zxv.Hexagrams.Polarity_Of (Tag) = Zxv.Hexagrams.Forbidden;

   function Count (L : Log) return Natural
     with Pre => Is_Open (L);
   function Head (L : Log) return Digest
     with Pre => Is_Open (L);

   type Record_View is record
      Seq       : Interfaces.Unsigned_64;
      Tag       : Zxv.Hexagrams.Hexagram;
      Reason    : Interfaces.Unsigned_8;
      Submitter : Interfaces.Unsigned_32;
      Subject   : Digest;
      Prev      : Digest;
      Hash      : Digest;
   end record;
   function Record_At (L : Log; Index : Natural) return Record_View
     with Pre => Is_Open (L);

   --  Recompute every link. A broken chain poisons the context (FATAL).
   procedure Verify (L : Log)
     with Pre => Is_Open (L);

   function Hex (D : Digest) return String
     with Post => Hex'Result'Length = 64;

private

   type Log is new Ada.Finalization.Limited_Controlled with record
      N : Node_Access;
   end record;
   overriding procedure Finalize (L : in out Log);

end Zxv.Authority.Quarantine;
