// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv

import (
	"fmt"
	"sync"
	"sync/atomic"
	"unsafe"
)

// handle owns one C pointer. Calls hold a read lock (or the write lock for
// handles that are not thread-safe in C), so Close never frees memory a
// call is using. A handle with live children refuses to close (R6: a child
// must be destroyed before its parent); finalizers run children first
// because a child references its parent.
type handle struct {
	mu        sync.RWMutex
	ptr       unsafe.Pointer
	what      string
	children  atomic.Int32
	parents   []*handle
	destroy   func(unsafe.Pointer)
	exclusive bool
}

func newHandle(what string, ptr unsafe.Pointer, destroy func(unsafe.Pointer), parents ...*handle) *handle {
	for _, p := range parents {
		p.children.Add(1)
	}
	return &handle{ptr: ptr, what: what, destroy: destroy, parents: parents}
}

// use runs f with the live pointer, or returns a closed error.
func (h *handle) use(f func(p unsafe.Pointer) error) error {
	if h.exclusive {
		h.mu.Lock()
		defer h.mu.Unlock()
	} else {
		h.mu.RLock()
		defer h.mu.RUnlock()
	}
	if h.ptr == nil {
		return &closedError{h.what}
	}
	return f(h.ptr)
}

// retain/release count an extra dependent (e.g. a session bound to a budget set).
func (h *handle) retain()  { h.children.Add(1) }
func (h *handle) release() { h.children.Add(-1) }

func (h *handle) close() error {
	h.mu.Lock()
	defer h.mu.Unlock()
	if h.ptr == nil {
		return nil
	}
	if n := h.children.Load(); n > 0 {
		return newError(StatusState, fmt.Sprintf("%s still has %d open dependent handle(s); close them first", h.what, n))
	}
	h.destroy(h.ptr)
	h.ptr = nil
	for _, p := range h.parents {
		p.release()
	}
	return nil
}
