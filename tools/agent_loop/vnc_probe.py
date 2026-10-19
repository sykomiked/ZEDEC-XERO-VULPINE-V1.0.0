#!/usr/bin/env python3
# vnc_probe.py — connect to a Contabo VNC console and read what the OS shows.
# The back door to the nine armada: the console is the OS we wrote, its boot
# log and its terminal. Minimal RFB: version, security, ServerInit, then a
# framebuffer capture written as a PNG-free PPM plus an ASCII downsample so
# the text is readable without an image viewer.
#
# usage: vnc_probe.py <host> <port> [--password PW] [--ppm OUT.ppm]
import socket
import struct
import sys
import time


def recv_exact(s, n):
    b = b""
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c:
            raise EOFError("closed after %d of %d" % (len(b), n))
        b += c
    return b


def main():
    host, port = sys.argv[1], int(sys.argv[2])
    pw = None
    ppm_out = None
    i = 3
    while i < len(sys.argv):
        if sys.argv[i] == "--password":
            pw = sys.argv[i + 1]; i += 2
        elif sys.argv[i] == "--ppm":
            ppm_out = sys.argv[i + 1]; i += 2
        else:
            i += 1

    s = socket.create_connection((host, port), timeout=20)
    s.settimeout(20)
    ver = recv_exact(s, 12)
    print("server:", ver.strip().decode(errors="replace"))
    s.sendall(b"RFB 003.008\n")

    n = recv_exact(s, 1)[0]
    types = list(recv_exact(s, n)) if n else []
    print("security types:", types, "(1=None 2=VNC-auth)")
    if 1 in types:
        s.sendall(bytes([1]))
    elif 2 in types:
        s.sendall(bytes([2]))
        challenge = recv_exact(s, 16)
        if pw is None:
            print("VNC auth required; pass --password")
            return 1
        key = pw.encode()[:8].ljust(8, b"\0")
        # the standard VNC key: bit-reverse each byte
        rev = bytes(int("{:08b}".format(b)[::-1], 2) for b in key)
        try:
            from Crypto.Cipher import DES
            des = DES.new(rev, DES.MODE_ECB)
            resp = des.encrypt(challenge)
        except ImportError:
            print("VNC auth needs pycryptodome (pip install pycryptodome)")
            return 1
        s.sendall(resp)
    else:
        print("no supported security type"); return 1

    res = struct.unpack(">I", recv_exact(s, 4))[0]
    print("security result:", res)
    if res != 0:
        return 1

    s.sendall(bytes([1]))  # shared
    w, h = struct.unpack(">HH", recv_exact(s, 4))
    pf = recv_exact(s, 16)
    bpp, depth, bigendian, truecolour = pf[0], pf[1], pf[2], pf[3]
    rmax, gmax, bmax = struct.unpack(">HHH", pf[4:10])
    rshift, gshift, bshift = pf[10], pf[11], pf[12]
    nlen = struct.unpack(">I", recv_exact(s, 4))[0]
    name = recv_exact(s, nlen).decode(errors="replace")
    print("framebuffer: %dx%d %dbpp depth=%d name=%r" % (w, h, bpp, depth, name))

    # the raw encoding, 32bpp
    s.sendall(struct.pack(">BBHBBBBHHHBBB", 0, 0, 0, 32, 24, 0, 1,
                          rmax, gmax, bmax, rshift, gshift, bshift, 0))
    s.sendall(struct.pack(">BHI", 2, 0, 1) + struct.pack(">I", 0))
    s.sendall(struct.pack(">BBHHHH", 3, 0, 0, 0, w, h))

    hdr = recv_exact(s, 4)
    nrect = struct.unpack(">H", hdr[2:4])[0]
    print("rects:", nrect)
    rows = {}
    for _ in range(nrect):
        x, y, rw, rh, enc = struct.unpack(">HHHHi", recv_exact(s, 12))
        if enc != 0:
            print("  unexpected encoding", enc); break
        data = recv_exact(s, rw * rh * 4)
        for r in range(rh):
            rows[y + r] = data[r * rw * 4:(r + 1) * rw * 4]

    if ppm_out and rows:
        with open(ppm_out, "wb") as f:
            f.write(b"P6\n%d %d\n255\n" % (w, h))
            for r in range(h):
                line = rows.get(r, b"\0" * w * 4)
                px = bytearray()
                for c in range(w):
                    v = struct.unpack(">I", line[c * 4:c * 4 + 4])[0]
                    px += bytes((((v >> rshift) & rmax) * 255 // max(rmax, 1),
                                 ((v >> gshift) & gmax) * 255 // max(gmax, 1),
                                 ((v >> bshift) & bmax) * 255 // max(bmax, 1)))
                f.write(bytes(px))
        print("wrote", ppm_out)

    # an ASCII downsample: bright pixels are text on the dark console
    print("\n--- the console (ASCII, 2px -> 1 char) ---")
    for r in range(0, h, 2):
        line = rows.get(r, b"\0" * w * 4)
        out = []
        for c in range(0, w, 2):
            v = struct.unpack(">I", line[c * 4:c * 4 + 4])[0]
            lum = (((v >> rshift) & rmax) + ((v >> gshift) & gmax) + ((v >> bshift) & bmax))
            out.append("#" if lum > (rmax + gmax + bmax) // 3 else ("+" if lum > 8 else " "))
        print("".join(out).rstrip())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
