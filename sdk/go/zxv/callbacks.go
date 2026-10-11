// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv

/*
#include <stdint.h>
#include <stddef.h>
*/
import "C"

import (
	"runtime/cgo"
	"unsafe"
)

// zxvGoOnToken is called by the C shim once per generated token, on the
// goroutine that called zxv_generate. h is a cgo.Handle to a *genState.
//
//export zxvGoOnToken
func zxvGoOnToken(h C.uintptr_t, token C.int32_t, piece *C.uint8_t, n C.size_t) C.int32_t {
	st, ok := cgo.Handle(h).Value().(*genState)
	if !ok {
		return 0
	}
	var b []byte
	if piece != nil && n > 0 {
		b = C.GoBytes(unsafe.Pointer(piece), C.int(n))
	}
	if st.onToken(Token{ID: int32(token), Piece: b}) {
		return 1
	}
	return 0
}

// zxvGoRmagEvent is the rmag listener; h is a cgo.Handle to a *budgetShared.
//
//export zxvGoRmagEvent
func zxvGoRmagEvent(h C.uintptr_t, budget C.uint32_t, event C.int32_t) {
	sh, ok := cgo.Handle(h).Value().(*budgetShared)
	if !ok {
		return
	}
	sh.event(uint32(budget), int32(event))
}
