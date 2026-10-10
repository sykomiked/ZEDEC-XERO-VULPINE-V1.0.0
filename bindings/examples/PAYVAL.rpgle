      // Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
      // SPDX-License-Identifier: Apache-2.0
      // PAYVAL.rpgle - ILE RPG binding SKETCH for the ZEDEC legacy bridge.
      // DOCUMENT ONLY: no RPG compiler is available in this container, so this
      // is not built or run here. It shows how an IBM i (ILE RPG) program would
      // prototype and call the libzxlegacy C ABI. Shapes match zx_legacy_api.h.

     ctl-opt main(PayVal);

      // C ABI prototypes. extproc names the C symbol; value = pass by value.
     dcl-pr zx_comp3_decode int(10) extproc('zx_comp3_decode');
       inbuf  pointer value;          // const uint8_t*
       nbytes int(10) value;          // int32_t
       outval pointer value;          // int64_t*
     end-pr;

     dcl-pr zx_legacy_abi_version int(10) extproc('zx_legacy_abi_version');
     end-pr;

     dcl-proc PayVal;
       dcl-s amount  packed(9:2) inz(12345.67);  // COMP-3 packed decimal
       dcl-s decoded int(20);                     // int64 scaled result
       dcl-s rc      int(10);

       rc = zx_comp3_decode(%addr(amount) : 5 : %addr(decoded));
       if rc = 0;
         dsply ('Decoded scaled amount: ' + %char(decoded));
       else;
         dsply ('Decode failed rc=' + %char(rc));
       endif;
     end-proc;
