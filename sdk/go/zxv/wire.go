// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv

/*
#include "zxv_api.h"
*/
import "C"

import (
	"encoding/binary"
	"encoding/hex"
	"unsafe"
)

// FrameBytes is the size of a UBH-168 frame.
const FrameBytes = 21

// WireTruth is what Ubh168Frame.Resolve reports.
type WireTruth int32

// Wire truth values.
const (
	WireUnknown WireTruth = 0
	WireTrue    WireTruth = 1
	WireFalse   WireTruth = 2
	WireNeutral WireTruth = 3
	WireGlut    WireTruth = 4 // contradiction held
	WireParadox WireTruth = 5 // persistent glut
)

// Ubh168Frame is a 21-octet UBH-168 frame: a tag, then five 32-bit words
// in the fixed order L-B-L-B-L (W0 little-endian S+, W1 big-endian S-, W2
// LE S+, W3 BE S-, W4 LE S+). Pack and Unpack are pure Go, so a service can
// frame without the native library.
type Ubh168Frame [FrameBytes]byte

// WireTag returns 100*sync + 10*shell + trunk (trunk, shell 0..9; sync 0..1).
func WireTag(trunk, shell, sync uint32) (uint8, error) {
	if trunk > 9 || shell > 9 || sync > 1 {
		return 0, newError(StatusArg, "trunk, shell 0..9 and sync 0..1")
	}
	return uint8(100*sync + 10*shell + trunk), nil
}

// PackFrame packs a tag, three S+ words and two S- words.
func PackFrame(tag uint8, sPlus [3]uint32, sMinus [2]uint32) Ubh168Frame {
	var f Ubh168Frame
	f[0] = tag
	binary.LittleEndian.PutUint32(f[1:5], sPlus[0])
	binary.BigEndian.PutUint32(f[5:9], sMinus[0])
	binary.LittleEndian.PutUint32(f[9:13], sPlus[1])
	binary.BigEndian.PutUint32(f[13:17], sMinus[1])
	binary.LittleEndian.PutUint32(f[17:21], sPlus[2])
	return f
}

// Unpack returns the tag and the words.
func (f Ubh168Frame) Unpack() (tag uint8, sPlus [3]uint32, sMinus [2]uint32) {
	return f[0],
		[3]uint32{binary.LittleEndian.Uint32(f[1:5]), binary.LittleEndian.Uint32(f[9:13]), binary.LittleEndian.Uint32(f[17:21])},
		[2]uint32{binary.BigEndian.Uint32(f[5:9]), binary.BigEndian.Uint32(f[13:17])}
}

// FrameFromBytes copies exactly 21 octets into a frame.
func FrameFromBytes(b []byte) (Ubh168Frame, error) {
	var f Ubh168Frame
	if len(b) != FrameBytes {
		return f, newError(StatusFormat, "a UBH-168 frame is 21 octets")
	}
	copy(f[:], b)
	return f, nil
}

// String returns the frame in hex.
func (f Ubh168Frame) String() string { return hex.EncodeToString(f[:]) }

// PackFrameNative packs with libzxv (for cross-checks).
func PackFrameNative(tag uint8, sPlus [3]uint32, sMinus [2]uint32) (Ubh168Frame, error) {
	var f Ubh168Frame
	sp := [3]C.uint32_t{C.uint32_t(sPlus[0]), C.uint32_t(sPlus[1]), C.uint32_t(sPlus[2])}
	sm := [2]C.uint32_t{C.uint32_t(sMinus[0]), C.uint32_t(sMinus[1])}
	err := call(func() C.int32_t {
		return C.zxv_wire_pack(C.uint8_t(tag), &sp[0], &sm[0], (*C.uint8_t)(unsafe.Pointer(&f[0])))
	})
	return f, err
}

// IdleFrame returns the idle pilot frame of a trunk (never all zeros).
func IdleFrame(trunk uint32) (Ubh168Frame, error) {
	var f Ubh168Frame
	err := call(func() C.int32_t { return C.zxv_wire_idle(C.uint32_t(trunk), (*C.uint8_t)(unsafe.Pointer(&f[0]))) })
	return f, err
}

// Resolve runs the W5 correlation resolver (words as signed Q16.16).
func (f Ubh168Frame) Resolve() (WireTruth, error) {
	var t C.int32_t
	err := call(func() C.int32_t { return C.zxv_wire_resolve((*C.uint8_t)(unsafe.Pointer(&f[0])), &t) })
	return WireTruth(t), err
}
