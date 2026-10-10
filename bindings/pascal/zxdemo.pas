{ Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC }
{ SPDX-License-Identifier: Apache-2.0 }
{ zxdemo.pas — Free Pascal example binding to the ZEDEC legacy bridge C ABI
  (libzxlegacy). Demonstrates HFP<->IEEE and the ISUP/SIP cause mapping.
  Halts with a non-zero code on mismatch. }
program zxdemo;
{$MODE OBJFPC}{$H+}
uses ctypes, sysutils;

function zx_legacy_abi_version: cint32; cdecl; external 'zxlegacy';
function zx_hfp64_to_ieee64(hfp: cuint64): cuint64; cdecl; external 'zxlegacy';
function zx_ieee64_to_hfp64(ieee: cuint64): cuint64; cdecl; external 'zxlegacy';
function zx_q850_to_sip_status(cause: cuint8): cint32; cdecl; external 'zxlegacy';

var
  ieee, back: cuint64;
  ok: Boolean;
begin
  WriteLn('ZEDEC legacy ABI version: ', zx_legacy_abi_version);
  ok := True;

  ieee := zx_hfp64_to_ieee64(cuint64($4110000000000000));
  WriteLn('HFP 1.0 -> IEEE: ', IntToHex(ieee, 16));
  if ieee <> cuint64($3FF0000000000000) then ok := False;

  back := zx_ieee64_to_hfp64(ieee);
  if back <> cuint64($4110000000000000) then ok := False;

  if zx_q850_to_sip_status(17) <> 486 then ok := False;

  if ok then
    WriteLn('Pascal binding checks OK')
  else
  begin
    WriteLn('Pascal binding MISMATCH');
    Halt(1);
  end;
end.
