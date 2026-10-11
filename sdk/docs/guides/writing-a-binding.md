<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Writing a binding for a new language

**Who this is for:** someone bringing ZXV to a language that has no SDK yet
(Kotlin/Native, Dart, Zig, Lua, Julia, a hardware vendor's toolchain), or
maintaining one of the existing SDKs.
**What you will be able to do:** call libzxv safely from any language with a
C foreign function interface (FFI), follow the rules every SDK follows, and
prove your binding agrees with the others.

## The contract you are binding to

libzxv's header, [`zxv_api.h`](../../libzxv/include/zxv_api.h), is a plain C
ABI built to be easy to bind. The full rules, R1 to R11, are in
[the C ABI](../ABI.md#the-rules). In one line each:

| Rule | In short |
|---|---|
| R1 | Handles are opaque pointers. Never look inside. |
| R2 | Arguments are fixed-width integers, pointers and plain structs only. |
| R3 | Every call returns an `int32_t` status. `0` is OK, negative is an error. |
| R4 | Text comes back through a two-call buffer. |
| R5 | Strings are UTF-8. |
| R6 | A context is thread-safe. Destroy children before parents. |
| R7 | `zxv_last_error` is per thread and for people only. |
| R8 | Check the version at load time. |
| R9 | Ask `zxv_features` what this build has. |
| R10 | Gated calls take a capability. Use the `*_cap` forms. |
| R11 | No floats. A host float crosses only as its raw 64-bit pattern. |

The [stability policy](../STABILITY.md) promises that nothing in a released
1.x header is ever removed or changed. Your binding keeps working with every
later 1.x library.

## A minimal binding in about 70 lines

This is a complete, working binding of a few functions in Python's `ctypes`
(no ZXV SDK involved). It shows the five things every binding must do.

```python
"""A minimal libzxv binding in plain ctypes: the shape every binding needs."""
import ctypes as C
import os
import struct

lib = C.CDLL(os.environ.get("ZXV_LIBRARY_PATH", "libzxv.so"))

# 1. Declare every function you call, with fixed-width types.
lib.zxv_version.argtypes = [C.POINTER(C.c_uint32)] * 3
lib.zxv_version.restype = C.c_int32
lib.zxv_status_name.argtypes = [C.c_int32]
lib.zxv_status_name.restype = C.c_char_p
lib.zxv_status_class.argtypes = [C.c_int32]
lib.zxv_status_class.restype = C.c_int32
lib.zxv_last_error.argtypes = [C.c_char_p, C.c_size_t, C.POINTER(C.c_size_t)]
lib.zxv_last_error.restype = C.c_int32
lib.zxv_rat_add.argtypes = [C.c_int64] * 4 + [C.POINTER(C.c_int64)] * 2
lib.zxv_rat_add.restype = C.c_int32
lib.zxv_rat_format.argtypes = [C.c_int64, C.c_int64, C.c_char_p, C.c_size_t, C.POINTER(C.c_size_t)]
lib.zxv_rat_format.restype = C.c_int32
lib.zxv_rat_from_ieee754.argtypes = [C.c_uint64, C.POINTER(C.c_int64), C.POINTER(C.c_int64)]
lib.zxv_rat_from_ieee754.restype = C.c_int32
lib.zxv_hex_name.argtypes = [C.c_uint8]          # uint8_t: unsigned, never a signed byte
lib.zxv_hex_name.restype = C.c_char_p

# 2. Refuse a library you were not written for (R8).
v = [C.c_uint32() for _ in range(3)]
lib.zxv_version(*(C.byref(x) for x in v))
major, minor = v[0].value, v[1].value
if major != 1 or minor < 3:
    raise ImportError(f"need libzxv 1.3 or a later 1.x, found {major}.{minor}")

# 3. Turn every negative status into an error, classified by zxv_status_class.
CLASSES = {1: "USAGE", 2: "REFUSED", 3: "FATAL", 4: "SYSTEM"}

class ZxvError(Exception):
    pass

def check(rc: int) -> None:
    if rc == 0:
        return
    buf, n = C.create_string_buffer(512), C.c_size_t()
    lib.zxv_last_error(buf, len(buf), C.byref(n))        # text is for people only
    raise ZxvError(f"{lib.zxv_status_name(rc).decode()} "
                   f"[{CLASSES.get(lib.zxv_status_class(rc), 'SYSTEM')}]: {buf.value.decode()}")

# 4. Read text with the two-call buffer pattern (R4).
def rat_format(num: int, den: int) -> str:
    need = C.c_size_t()
    lib.zxv_rat_format(num, den, None, 0, C.byref(need))  # ZXV_E_BUFFER, need set
    buf = C.create_string_buffer(need.value + 1)          # + 1 for the NUL
    check(lib.zxv_rat_format(num, den, buf, len(buf), C.byref(need)))
    return buf.value.decode()

n, d = C.c_int64(), C.c_int64()
check(lib.zxv_rat_add(1, 10, 2, 10, C.byref(n), C.byref(d)))
print(rat_format(n.value, d.value))                       # 0.3

# 5. A host float crosses only as its raw IEEE-754 bits (R11).
bits = struct.unpack("<Q", struct.pack("<d", 0.1))[0]
check(lib.zxv_rat_from_ieee754(bits, C.byref(n), C.byref(d)))
print(rat_format(n.value, d.value))                       # 3602879701896397/36028797018963968

print(lib.zxv_hex_name(60).decode())                      # true/negentropic/potential
try:
    check(lib.zxv_rat_add(1, 0, 1, 1, C.byref(n), C.byref(d)))
except ZxvError as e:
    print(e)                                              # ZXV_E_ARG [USAGE]: a has a zero denominator
```

Run it with the full path to the library:

```sh
ZXV_LIBRARY_PATH=$PWD/sdk/libzxv/build/libzxv.so python3 mini_binding.py
```

## Rules that bite binding authors

**Narrow unsigned arguments.** Some functions take a `uint8_t` or `uint16_t`
by value (a hexagram tag, a wire tag). Pass them as **unsigned** values in
range. If your FFI only has a signed byte type, declare the parameter as a
32-bit int and pass 0 to 255 instead. On Apple arm64 the caller is
responsible for extending narrow arguments to the full register, and the
callee trusts it. A signed byte `0xC7` gets sign-extended and libzxv sees
4,294,967,239, so a valid tag such as 199 is refused. The Java SDK hit
exactly this and now passes these arguments as in-range `int`s.

**Out-pointers are written only on success.** Do not read an out value after
a failure, with two documented exceptions: `*out_len` on `ZXV_E_BUFFER`
(that is how the two-call pattern works), and `zxv_swarm_consume`'s
`*out_granted` on `ZXV_E_BUDGET`.

**Text needs `+ 1`; arrays and bytes do not.** The `+ 1` is room for the NUL
at the end of a string. Calls that return arrays or raw bytes give the exact
count. See [R4](../ABI.md#r4-two-call-buffers).

**Destroy in order.** Children before parents: an attenuated capability
before its parent, every handle before its context. In a garbage-collected
language, keep a reference from child to parent so the parent cannot be
finalised first. See [R6](../ABI.md#r6-threading).

**Fail-closed.** After a FATAL status, mark your context object dead and
raise a terminal error type that ordinary handlers do not catch, if your
language has one. Offer an opt-in abort. See
[Errors and fail-closed](errors-and-fail-closed.md).

**Same names.** Use the nouns in [SDK_DESIGN §2](../SDK_DESIGN.md#2-names) so
people can move between languages.

## Prove it agrees

1. **Conformance vectors.** Test your binding against
   [`vectors.json`](../../conformance/vectors.json), the answer key every
   SDK uses. See [Conformance testing](conformance-testing.md).
2. **ABI drift.** If your binding declares functions by hand (like the
   example above) rather than generating them from the header, add a test
   that compares your declarations with `zxv_api.h`, so a typo in a
   signature fails your build instead of corrupting memory. The library's
   own check, [`abi_check.py`](../../libzxv/tools/abi_check.py), shows how
   to read every prototype and constant from the header:

   ```sh
   python3 sdk/libzxv/tools/abi_check.py check sdk/libzxv/include/zxv_api.h \
       sdk/libzxv/abi/libzxv-1.1.1.json sdk/libzxv/build/libzxv.so
   # ABI check against 1.1.1: ok (158 functions, 179 constants unchanged; 37 functions added)
   ```

3. **Sanitizers.** Run your tests against a libzxv built with
   AddressSanitizer and UndefinedBehaviorSanitizer:
   `make -C sdk/libzxv SANITIZE=1 OUT=build/san all`. They catch the
   out-of-bounds reads and writes a wrong declaration can cause. When the
   host is an interpreter (Python, Ruby, Node), you usually have to preload
   the sanitizer runtime, for example
   `LD_PRELOAD=$(gcc -print-file-name=libasan.so)` on Linux.

## Next

- [The C ABI](../ABI.md) and [the stability policy](../STABILITY.md)
- [Conformance testing](conformance-testing.md)
