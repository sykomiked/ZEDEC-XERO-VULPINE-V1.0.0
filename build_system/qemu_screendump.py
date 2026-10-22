#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""qemu_screendump.py: save what a headless QEMU's screen shows, as a PNG.

    qemu_screendump.py <monitor.sock> <delay-seconds> <out.png>

Waits, asks the QEMU human monitor on a unix socket for a `screendump`
(a PPM), and writes it out as a PNG with the standard library only (zlib,
no imaging package). Used by uefi_boot_check.sh when UEFI_SCREENSHOT is
set, so a headless boot (CI, verify_boot.sh) still leaves a picture of the
screen next to its serial log. Exits 0 even when QEMU has already gone:
a missing screenshot is reported, never a reason to fail the boot check.
"""
import os
import socket
import struct
import sys
import tempfile
import time
import zlib


def ppm_to_png(ppm: bytes) -> bytes:
    # P6 header: magic, width, height, maxval, each separated by whitespace
    # (comments are not produced by QEMU).
    parts, i = [], 0
    while len(parts) < 4:
        while ppm[i : i + 1].isspace():
            i += 1
        j = i
        while not ppm[j : j + 1].isspace():
            j += 1
        parts.append(ppm[i:j])
        i = j
    i += 1  # the single whitespace byte after maxval
    if parts[0] != b"P6" or int(parts[3]) != 255:
        raise ValueError("not an 8-bit P6 PPM")
    w, h = int(parts[1]), int(parts[2])
    pix = ppm[i : i + w * h * 3]
    if len(pix) != w * h * 3:
        raise ValueError("short PPM")
    raw = b"".join(b"\x00" + pix[y * w * 3 : (y + 1) * w * 3] for y in range(h))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))

    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        return 2
    sock_path, delay, out = sys.argv[1], float(sys.argv[2]), sys.argv[3]
    time.sleep(delay)
    fd, ppm_path = tempfile.mkstemp(suffix=".ppm")
    os.close(fd)
    try:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(5)
        s.connect(sock_path)
        s.sendall(("screendump %s\n" % ppm_path).encode())
        for _ in range(50):  # the dump is written asynchronously
            time.sleep(0.1)
            if os.path.getsize(ppm_path) > 0:
                break
        time.sleep(0.3)
        s.close()
        with open(ppm_path, "rb") as f:
            png = ppm_to_png(f.read())
        with open(out, "wb") as f:
            f.write(png)
        print("[screendump] saved %s" % out)
    except (OSError, ValueError, IndexError) as e:
        print("[screendump] no screenshot: %s" % e)
    finally:
        os.unlink(ppm_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
