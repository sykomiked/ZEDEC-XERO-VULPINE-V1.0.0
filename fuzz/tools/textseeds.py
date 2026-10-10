# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Write the text seed corpora (copybooks, manifests, version / time / name
strings, CID strings). The ISO 20022 XML seeds are the documents test_pay
writes (kernel/src/pay/test_pay.c, third argument) and are copied by hand.

    python3 -I fuzz/tools/textseeds.py fuzz/corpus
"""
import os
import struct
import sys


def put(root, target, name, data):
    d = os.path.join(root, target)
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "seed-" + name), "wb") as f:
        f.write(data)


def main(root):
    cb = (b"01  TXN-RECORD.\n"
          b"    05  TXN-ID          PIC 9(6).\n"
          b"    05  ACCT-NAME       PIC X(20).\n"
          b"    05  AMOUNT          PIC S9(9)V99 COMP-3.\n"
          b"    05  RAIL-CODE       PIC 9(4) COMP.\n"
          b"    05  HIST-AMT        PIC S9(7)V99 COMP-3 OCCURS 3 TIMES.\n"
          b"    05  ALT-ID          REDEFINES TXN-ID PIC X(6).\n")
    rec = (b"001234" + b"ALICE".ljust(20) + bytes([0, 0, 0, 1, 0x23, 0x45, 0x0C]) +
           b"\x00\x2a" + bytes([0, 0, 1, 0, 0x0C]) * 3)
    put(root, "fuzz_cobol_copybook", "txn", struct.pack("<H", len(cb)) + cb + rec)
    cb2 = b"01 R.\n   05 A PIC S9(4) COMP.\n   05 B PIC 9(3)V9.\n   05 C PIC X(3) OCCURS 2 TIMES.\n"
    rdw = b"\x00\x08\x00\x00ABCD\x00\x06\x00\x00EF"
    put(root, "fuzz_cobol_copybook", "rdw", struct.pack("<H", len(cb2)) + cb2 + rdw)

    m = (b"zxv-update-manifest 1\nrelease 1.2.0\nmin-version 1.0.0\nissued 5\n"
         b"entry kernel x86_64 1.2.0 3000 "
         b"bafkreiell7aotnkzvtmgusibpfbxa7ct4kb7e25wfhfsbphjco5mtf24ee "
         b"8b5fc0e9b559acd86a49017943707c53e283f26bb629cb20bce913bac9975c21 a/k.bin\n"
         b"end\n")
    put(root, "fuzz_upcheck", "manifest", b"\x00" + m)
    put(root, "fuzz_upcheck", "version", b"\x02" + b"1.20.3")
    put(root, "fuzz_upcheck", "rfc3339", b"\x02" + b"2026-10-10T12:34:56.123456789Z")
    put(root, "fuzz_upcheck", "ipns-name",
        b"\x02" + b"k51qzi5uqu5dlvj2baxnqndepeb86cbk3ng7n3i46uzyxzyqj2xjonzllnv0v8")

    put(root, "fuzz_ipfs_cid", "v1-dagpb",
        b"bafybeigdyrzt5sfp7udm7hu76uh7y26nf3efuylqabf3oclgtqy55fbzdi")
    put(root, "fuzz_ipfs_cid", "varint", bytes([0xAC, 0x02]))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "corpus")
