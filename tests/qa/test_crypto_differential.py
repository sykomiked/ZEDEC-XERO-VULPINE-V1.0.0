# SPDX-License-Identifier: Apache-2.0
"""Independent hashlib/HMAC oracle at block, padding and old truncation limits."""
import ctypes
import hashlib
import hmac
import sys

lib=ctypes.CDLL(sys.argv[1])
lib.sha256.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p]
lib.hmac_sha256.argtypes=[ctypes.c_void_p,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_uint32,ctypes.c_void_p]
lib.hkdf_expand.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_uint32]
lib.hkdf_expand.restype=ctypes.c_bool
checks=0
for length in (0,1,55,56,63,64,65,255,256,257,511,512,513,1024,4096):
    msg=bytes((i*17+3)%256 for i in range(length))
    out=ctypes.create_string_buffer(32)
    lib.sha256(msg,length,out)
    assert out.raw == hashlib.sha256(msg).digest(), ('SHA256',length)
    checks+=1
    for keylength in (0,1,63,64,65,131):
        key=bytes((i*7+1)%256 for i in range(keylength))
        lib.hmac_sha256(key,keylength,msg,length,out)
        assert out.raw == hmac.digest(key,msg,'sha256'), ('HMAC',keylength,length)
        checks+=1
for length in (0,1,31,32,33,255,256,8160):
    prk=bytes(range(32)); info=b'context'
    previous=b''; expected=b''
    for counter in range(1,(length+31)//32+1):
        previous=hmac.digest(prk,previous+info+bytes([counter]),'sha256')
        expected+=previous
    out=ctypes.create_string_buffer(max(1,length))
    assert lib.hkdf_expand(prk,info,len(info),out,length)
    assert out.raw[:length] == expected[:length], ('HKDF',length)
    checks+=1
print(f'crypto differential: {checks} independent oracle checks passed')
