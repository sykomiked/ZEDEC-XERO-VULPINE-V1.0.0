// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv

/*
#cgo CFLAGS: -I${SRCDIR}/../../libzxv/include
#cgo LDFLAGS: -L${SRCDIR}/../../libzxv/build -lzxv -Wl,-rpath,${SRCDIR}/../../libzxv/build
#include <stdlib.h>
#include "zxv_api.h"
*/
import "C"

import (
	"runtime"
	"unsafe"
)

// lastError copies the calling thread's last-error detail (R7). The caller
// must hold the OS thread (runtime.LockOSThread) since the failing call.
func lastError() string {
	var n C.size_t
	buf := make([]byte, 256)
	for i := 0; i < 2; i++ {
		rc := C.zxv_last_error((*C.char)(unsafe.Pointer(&buf[0])), C.size_t(len(buf)), &n)
		if rc == C.ZXV_OK {
			return string(buf[:n])
		}
		if rc != C.ZXV_E_BUFFER {
			break
		}
		buf = make([]byte, int(n)+1)
	}
	return ""
}

// call runs one C call on a locked OS thread and, on failure, reads the
// last-error detail on that same thread. Goroutines can move between
// threads between cgo calls, so the detail must be read before unlocking.
func call(f func() C.int32_t) error {
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()
	rc := f()
	if rc == C.ZXV_OK {
		return nil
	}
	return newError(int32(rc), lastError())
}

// readString runs an R4 two-call text function on a locked thread.
func readString(f func(buf *C.char, cap C.size_t, outLen *C.size_t) C.int32_t) (string, error) {
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()
	buf := make([]byte, 64)
	for {
		var n C.size_t
		rc := f((*C.char)(unsafe.Pointer(&buf[0])), C.size_t(len(buf)), &n)
		if rc == C.ZXV_OK {
			return string(buf[:n]), nil
		}
		if rc == C.ZXV_E_BUFFER && int(n)+1 > len(buf) {
			buf = make([]byte, int(n)+1)
			continue
		}
		return "", newError(int32(rc), lastError())
	}
}

// cstring allocates a C copy of s; free it with C.free. Strings with a NUL
// byte are refused.
func cstring(s string) (*C.char, error) {
	for i := 0; i < len(s); i++ {
		if s[i] == 0 {
			return nil, newError(int32(C.ZXV_E_ARG), "string contains a NUL byte")
		}
	}
	return C.CString(s), nil
}

func freeC(p *C.char) {
	if p != nil {
		C.free(unsafe.Pointer(p))
	}
}

// bytePtr returns a pointer to b's first byte, or nil for an empty slice.
func bytePtr(b []byte) *C.uint8_t {
	if len(b) == 0 {
		return nil
	}
	return (*C.uint8_t)(unsafe.Pointer(&b[0]))
}
