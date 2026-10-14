// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv

// Freight packets (ABI 1.5): 168 erasure-coded 21-byte rows, sets that
// split and join larger payloads, and op streams.
//
// A freight carries up to 3360 payload bytes as 168 smart packets of 21
// bytes: byte 0 is the row index 0..167, bytes 1..20 are row data. Rows
// 0..k-1 hold the payload verbatim (zero padded); rows k..167 are Cauchy
// Reed-Solomon parity over GF(2^8). ANY k distinct rows rebuild the payload,
// so up to 168-k rows may be lost. Choosing k is the trade: payload per
// freight is k*20 bytes, loss tolerance is 168-k rows.
//
// The 64-byte FreightHeader travels separately (in the outer message) and
// names the payload's SHA-256. A payload larger than one freight is split
// into a SET of up to FreightMaxSet freights that share the freight id.
//
// The payload may be an OP STREAM (LITERAL, COPY, CIDREF and SEED ops) that
// the receiver expands. Expansion checks the declared total against the
// caller's limit before writing a byte, so a stream cannot expand past what
// the receiver allowed.
//
// Everything here is pure: no capability, no context. FreightJoin and
// FreightOpsWriter hold only their own memory; each is safe to share
// between goroutines (calls on one are serialized).
//
// Honest limits: erasure coding only. A lost row is recovered; a CORRUPTED
// row is detected (the payload SHA-256 fails, ErrVerificationFailed) but not located or
// repaired: drop rows you distrust and decode again. The header is neither
// protected by the code nor authenticated: sign it (or the outer message)
// if the sender matters. No network transport: moving packets is up to you.
// Parity makes a freight larger than its payload (3528 wire bytes for at
// most 3360). The op stream promises no compression: random data encodes
// slightly LARGER than itself, COPY helps only data that repeats, CIDREF
// only names bytes the receiver must already hold, and SEED only reproduces
// the built-in generators. No information is created.

/*
#include <stdlib.h>
#include "zxv_api.h"
#include "shim.h"
*/
import "C"

import (
	"runtime"
	"runtime/cgo"
	"unsafe"
)

// Freight sizes.
const (
	FreightPacketBytes = int(C.ZXV_FREIGHT_PACKET_BYTES) // 21: one smart packet
	FreightRowPayload  = int(C.ZXV_FREIGHT_ROW_PAYLOAD)  // 20 data bytes per row
	FreightRows        = int(C.ZXV_FREIGHT_ROWS)         // 168 rows per freight
	FreightBytes       = int(C.ZXV_FREIGHT_BYTES)        // 3528: one encoded freight
	FreightMaxPayload  = int(C.ZXV_FREIGHT_MAX_PAYLOAD)  // 3360, at k = 168
	FreightHeaderBytes = int(C.ZXV_FREIGHT_HEADER_BYTES) // 64
	FreightVersion     = uint8(C.ZXV_FREIGHT_VERSION)    // 1
	FreightMaxSet      = uint32(C.ZXV_FREIGHT_MAX_SET)   // 16384 freights in one set
	// FreightSetMaxBytes is the largest set payload, FreightMaxSet * 3360.
	FreightSetMaxBytes = uint64(16384 * 3360)
	// FreightFlagOpStream marks a (set) payload that is an op stream.
	FreightFlagOpStream = uint8(C.ZXV_FREIGHT_F_OPSTREAM)
	// FreightCIDMax is the longest content id a CIDREF op may name.
	FreightCIDMax = int(C.ZXV_FREIGHT_CID_MAX)
	// FreightGenParamMax is the longest SEED parameter block.
	FreightGenParamMax = int(C.ZXV_FREIGHT_GEN_PARAM_MAX)
	// FreightOpsMax is the largest op stream or expansion handled (64 MiB).
	FreightOpsMax = uint64(64 << 20)
)

// FreightGen names a built-in SEED generator.
type FreightGen uint8

// The SEED generators and their params.
const (
	// FreightGenXorshift: 8 bytes, a nonzero u64 LE seed; xorshift64* bytes LE.
	FreightGenXorshift FreightGen = C.ZXV_FREIGHT_GEN_XORSHIFT
	// FreightGenRepeat: 1..32 bytes, the pattern, repeated.
	FreightGenRepeat FreightGen = C.ZXV_FREIGHT_GEN_REPEAT
	// FreightGenFibWord: 2 distinct bytes a, b: the Fibonacci word abaababaab...
	FreightGenFibWord FreightGen = C.ZXV_FREIGHT_GEN_FIBWORD
	// FreightGenSierpinski: 3 bytes, log2 width w (1..15), on, off.
	FreightGenSierpinski FreightGen = C.ZXV_FREIGHT_GEN_SIERPINSKI
)

// FreightHeader is a serialized 64-byte freight header: "ZXFH", version, k,
// flags, payload length, seq, seq count, freight id, expand limit and the
// payload's SHA-256. It is a value type; compare headers with ==.
type FreightHeader [64]byte

// FreightHeaderFields are the fields of a FreightHeader.
type FreightHeaderFields struct {
	Version       uint8    // always FreightVersion
	K             uint8    // data rows, 1..168
	Flags         uint8    // 0 or FreightFlagOpStream
	PayloadLen    uint16   // bytes in this freight, at most K*20
	Seq           uint16   // member index in its set
	SeqCount      uint16   // members in the set, 1..16384
	FreightID     uint64   // shared by every member of a set
	ExpandLimit   uint64   // 0 unless Flags has FreightFlagOpStream
	PayloadSHA256 [32]byte // SHA-256 of this freight's payload
}

func (h *FreightHeader) ptr() *C.uint8_t { return (*C.uint8_t)(unsafe.Pointer(&h[0])) }

// NewFreightHeader builds the header of a single freight (seq 0 of 1)
// carrying payload with k data rows (1..168); it hashes the payload.
// expandLimit must be 0 unless flags has FreightFlagOpStream (ZXV_E_ARG).
// ZXV_E_TOO_LONG when payload is over k*20 bytes.
func NewFreightHeader(freightID uint64, k, flags uint8, expandLimit uint64, payload []byte) (FreightHeader, error) {
	var h FreightHeader
	err := call(func() C.int32_t {
		return C.zxv_freight_header_init(C.uint64_t(freightID), C.uint8_t(k), C.uint8_t(flags),
			C.uint64_t(expandLimit), bytePtr(payload), C.size_t(len(payload)), h.ptr())
	})
	return h, err
}

// Serialize builds a header from its fields (Version is ignored: it is
// always FreightVersion). Any field that fails the header check is ZXV_E_ARG.
func (f FreightHeaderFields) Serialize() (FreightHeader, error) {
	var h FreightHeader
	sha := f.PayloadSHA256
	err := call(func() C.int32_t {
		return C.zxv_freight_header_serialize(C.uint8_t(f.K), C.uint8_t(f.Flags), C.uint16_t(f.PayloadLen),
			C.uint16_t(f.Seq), C.uint16_t(f.SeqCount), C.uint64_t(f.FreightID), C.uint64_t(f.ExpandLimit),
			ptr32(&sha), h.ptr())
	})
	return h, err
}

// Fields parses and checks the header. ZXV_E_FORMAT for a bad magic, nonzero
// reserved bytes or a field that fails the check.
func (h FreightHeader) Fields() (FreightHeaderFields, error) {
	var f FreightHeaderFields
	var ver, k, fl C.uint8_t
	var pl, seq, cnt C.uint16_t
	var id, lim C.uint64_t
	err := call(func() C.int32_t {
		return C.zxv_freight_header_parse(h.ptr(), &ver, &k, &fl, &pl, &seq, &cnt, &id, &lim, ptr32(&f.PayloadSHA256))
	})
	if err != nil {
		return FreightHeaderFields{}, err
	}
	f.Version, f.K, f.Flags = uint8(ver), uint8(k), uint8(fl)
	f.PayloadLen, f.Seq, f.SeqCount = uint16(pl), uint16(seq), uint16(cnt)
	f.FreightID, f.ExpandLimit = uint64(id), uint64(lim)
	return f, nil
}

// Check returns nil if the header is well formed, else ZXV_E_FORMAT.
func (h FreightHeader) Check() error {
	return call(func() C.int32_t { return C.zxv_freight_header_check(h.ptr()) })
}

// FreightEncode encodes the 168 smart packets (3528 bytes, rows in order)
// of the freight described by h. payload must be exactly what the header
// was built for: a length mismatch is ZXV_E_ARG, a hash mismatch ErrVerificationFailed.
func FreightEncode(h FreightHeader, payload []byte) ([]byte, error) {
	out := make([]byte, FreightBytes)
	err := call(func() C.int32_t {
		return C.zxv_freight_encode(h.ptr(), bytePtr(payload), C.size_t(len(payload)), bytePtr(out))
	})
	if err != nil {
		return nil, err
	}
	return out, nil
}

// FreightDecode rebuilds the payload from whatever packets arrived:
// packets holds len/21 packets of 21 bytes each, in any order; duplicates
// and row indices above 167 are ignored. ErrErasure: fewer than k distinct
// rows (wait for more and decode again). ErrVerificationFailed: the rebuilt payload
// fails the header's SHA-256 (a corrupted row or another freight's header).
func FreightDecode(h FreightHeader, packets []byte) ([]byte, error) {
	return readBytes(FreightMaxPayload, func(buf *C.uint8_t, cp C.size_t, n *C.size_t) C.int32_t {
		return C.zxv_freight_decode(h.ptr(), bytePtr(packets), C.size_t(len(packets)), buf, cp, n)
	})
}

// FreightSetCount returns how many freights carry n bytes at k (0 bytes
// still needs one). ZXV_E_TOO_LONG when more than FreightMaxSet are needed.
func FreightSetCount(n uint64, k uint8) (uint32, error) {
	var c C.uint32_t
	err := call(func() C.int32_t { return C.zxv_freight_set_count(C.uint64_t(n), C.uint8_t(k), &c) })
	return uint32(c), err
}

// FreightSplit builds member seq of the set that carries data: its header
// and its 168 packets (3528 bytes). ZXV_E_RANGE when seq is not below the set
// count; ZXV_E_TOO_LONG when the set would exceed FreightMaxSet freights.
func FreightSplit(freightID uint64, k, flags uint8, expandLimit uint64, data []byte, seq uint32) (FreightHeader, []byte, error) {
	var h FreightHeader
	out := make([]byte, FreightBytes)
	err := call(func() C.int32_t {
		return C.zxv_freight_split(C.uint64_t(freightID), C.uint8_t(k), C.uint8_t(flags), C.uint64_t(expandLimit),
			bytePtr(data), C.size_t(len(data)), C.uint32_t(seq), h.ptr(), bytePtr(out))
	})
	if err != nil {
		return FreightHeader{}, nil, err
	}
	return h, out, nil
}

// FreightJoin reassembles one set, member by member, in any order. The
// first member accepted fixes the set (id, k, flags, count, expand limit).
// Close it when done; calls after Close return ErrClosed.
type FreightJoin struct{ h *handle }

// NewFreightJoin creates a join with a buffer of capacity bytes
// (0..FreightSetMaxBytes, else ZXV_E_RANGE).
func NewFreightJoin(capacity uint64) (*FreightJoin, error) {
	var p *C.zxv_freight_join
	if err := call(func() C.int32_t { return C.zxv_freight_join_create(C.uint64_t(capacity), &p) }); err != nil {
		return nil, err
	}
	j := &FreightJoin{h: newHandle("FreightJoin", unsafe.Pointer(p), func(p unsafe.Pointer) {
		C.zxv_freight_join_destroy((*C.zxv_freight_join)(p))
	})}
	j.h.exclusive = true
	runtime.SetFinalizer(j, func(j *FreightJoin) { _ = j.h.close() })
	return j, nil
}

// Close frees the join and its buffer. Closing twice is a no-op.
func (j *FreightJoin) Close() error { return j.h.close() }

// Add decodes one member into its place. A member already received is a
// no-op. ZXV_E_STATE: the member belongs to another set, or a non-last member
// is not full. ZXV_E_CAPACITY: the whole set cannot fit the join's capacity.
// ErrErasure, ErrVerificationFailed, ZXV_E_FORMAT as for FreightDecode; a failed member is
// not recorded, so add it again when more rows arrive.
func (j *FreightJoin) Add(h FreightHeader, packets []byte) error {
	return j.h.use(func(p unsafe.Pointer) error {
		return call(func() C.int32_t {
			return C.zxv_freight_join_add((*C.zxv_freight_join)(p), h.ptr(), bytePtr(packets), C.size_t(len(packets)))
		})
	})
}

// FreightJoinStatus is a join's progress.
type FreightJoinStatus struct {
	Received uint32 // members accepted so far
	Count    uint32 // members in the set; 0 until the first is accepted
	Complete bool   // every member arrived
	Total    uint64 // the joined length once complete, else 0
}

// Status returns the join's progress.
func (j *FreightJoin) Status() (FreightJoinStatus, error) {
	var r, c C.uint32_t
	var done C.int32_t
	var t C.uint64_t
	err := j.h.use(func(p unsafe.Pointer) error {
		return call(func() C.int32_t { return C.zxv_freight_join_status((*C.zxv_freight_join)(p), &r, &c, &done, &t) })
	})
	return FreightJoinStatus{uint32(r), uint32(c), done != 0, uint64(t)}, err
}

// Result returns the joined bytes. ZXV_E_STATE until every member arrived.
func (j *FreightJoin) Result() ([]byte, error) {
	var out []byte
	err := j.h.use(func(p unsafe.Pointer) error {
		var e error
		out, e = readBytes(256, func(buf *C.uint8_t, cp C.size_t, n *C.size_t) C.int32_t {
			return C.zxv_freight_join_result((*C.zxv_freight_join)(p), buf, cp, n)
		})
		return e
	})
	return out, err
}

// FreightOpsPeek reads an op stream's declared total and whether it
// carries an output SHA-256, without expanding it. ZXV_E_FORMAT: bad header.
func FreightOpsPeek(ops []byte) (total uint64, hasSHA bool, err error) {
	var t C.uint64_t
	var s C.int32_t
	err = call(func() C.int32_t { return C.zxv_freight_ops_peek(bytePtr(ops), C.size_t(len(ops)), &t, &s) })
	return uint64(t), s != 0, err
}

// FreightResolver supplies the bytes a CIDREF op names: it fills out (whose
// length is the op's length) with the bytes of content id cid and returns
// nil, or returns an error if it cannot. It runs on the calling goroutine
// during FreightOpsExpand and must not call FreightOpsExpand itself. A
// panic in it is recovered and treated as "cannot resolve".
type FreightResolver func(cid []byte, out []byte) error

type freightResolve struct{ fn FreightResolver }

//export zxvGoFreightResolve
func zxvGoFreightResolve(h C.uintptr_t, cid *C.uint8_t, cidLen C.size_t, out *C.uint8_t, n C.uint64_t) (rc C.int32_t) {
	defer func() {
		if recover() != nil {
			rc = C.ZXV_E_INTERNAL
		}
	}()
	st, ok := cgo.Handle(h).Value().(*freightResolve)
	if !ok || st.fn == nil {
		return C.ZXV_E_NOT_FOUND
	}
	var c, o []byte
	if cid != nil && cidLen > 0 {
		c = C.GoBytes(unsafe.Pointer(cid), C.int(cidLen))
	}
	if out != nil && n > 0 {
		o = unsafe.Slice((*byte)(unsafe.Pointer(out)), int(n))
	}
	if err := st.fn(c, o); err != nil {
		return C.ZXV_E_NOT_FOUND
	}
	return C.ZXV_OK
}

// FreightOpsExpand expands an op stream, accepting at most limit bytes
// (limit <= FreightOpsMax, else ZXV_E_RANGE): a declared total above limit is
// ZXV_E_TOO_LONG before anything is produced. resolve may be nil (a CIDREF then
// fails with ErrNotFound). ZXV_E_FORMAT: a malformed stream. ErrVerificationFailed: the
// output fails the stream's SHA-256.
func FreightOpsExpand(ops []byte, limit uint64, resolve FreightResolver) ([]byte, error) {
	hint := 1
	if total, _, err := FreightOpsPeek(ops); err == nil && total <= limit && total <= FreightOpsMax {
		hint = int(total)
	}
	var hv C.uintptr_t
	if resolve != nil {
		ch := cgo.NewHandle(&freightResolve{fn: resolve})
		defer ch.Delete()
		hv = C.uintptr_t(ch)
	}
	return readBytes(hint, func(buf *C.uint8_t, cp C.size_t, n *C.size_t) C.int32_t {
		return C.zxv_go_freight_ops_expand(bytePtr(ops), C.size_t(len(ops)), C.uint64_t(limit), hv, buf, cp, n)
	})
}

// FreightGenRun runs one SEED generator directly and returns n bytes.
// ZXV_E_ARG: params over 32 bytes, an unknown generator, or params the
// generator refuses.
func FreightGenRun(gen FreightGen, params []byte, n int) ([]byte, error) {
	if n < 0 {
		return nil, newError(StatusArg, "negative length")
	}
	out := make([]byte, n)
	err := call(func() C.int32_t {
		return C.zxv_freight_gen_run(C.uint8_t(gen), bytePtr(params), C.size_t(len(params)), bytePtr(out), C.size_t(n))
	})
	if err != nil {
		return nil, err
	}
	return out, nil
}

// FreightOpsEncode encodes in as a LITERAL/COPY op stream (greedy LZ77,
// deterministic), with the output SHA-256 when withSHA. The result may be
// LARGER than the input. ZXV_E_TOO_LONG: in is over FreightOpsMax.
func FreightOpsEncode(in []byte, withSHA bool) ([]byte, error) {
	return readBytes(len(in)/2+64, func(buf *C.uint8_t, cp C.size_t, n *C.size_t) C.int32_t {
		return C.zxv_freight_ops_encode(bytePtr(in), C.size_t(len(in)), boolInt(withSHA), buf, cp, n)
	})
}

// FreightOpsBound returns the worst-case encoded size of n input bytes.
func FreightOpsBound(n uint64, withSHA bool) (uint64, error) {
	var b C.uint64_t
	err := call(func() C.int32_t { return C.zxv_freight_ops_bound(C.uint64_t(n), boolInt(withSHA), &b) })
	return uint64(b), err
}

// FreightOpsWriter builds an op stream op by op. The first error of a put
// is sticky: every later put and Finish return it again. Close it when
// done; calls after Close return ErrClosed.
type FreightOpsWriter struct{ h *handle }

// NewFreightOpsWriter creates a writer for a stream whose ops must produce
// declaredTotal bytes in all, into a buffer of maxBytes (14, or 46 with a
// SHA, up to FreightOpsMax). sha256, if not nil, is the SHA-256 of the
// output, which expansion will verify.
func NewFreightOpsWriter(declaredTotal uint64, sha256 *[32]byte, maxBytes uint64) (*FreightOpsWriter, error) {
	var p *C.zxv_freight_ops_writer
	var sp *C.uint8_t
	var sha [32]byte
	if sha256 != nil {
		sha = *sha256
		sp = ptr32(&sha)
	}
	if err := call(func() C.int32_t {
		return C.zxv_freight_ops_writer_create(C.uint64_t(declaredTotal), sp, C.uint64_t(maxBytes), &p)
	}); err != nil {
		return nil, err
	}
	w := &FreightOpsWriter{h: newHandle("FreightOpsWriter", unsafe.Pointer(p), func(p unsafe.Pointer) {
		C.zxv_freight_ops_writer_destroy((*C.zxv_freight_ops_writer)(p))
	})}
	w.h.exclusive = true
	runtime.SetFinalizer(w, func(w *FreightOpsWriter) { _ = w.h.close() })
	return w, nil
}

// Close frees the writer. Closing twice is a no-op.
func (w *FreightOpsWriter) Close() error { return w.h.close() }

func (w *FreightOpsWriter) put(f func(p *C.zxv_freight_ops_writer) C.int32_t) error {
	return w.h.use(func(p unsafe.Pointer) error {
		return call(func() C.int32_t { return f((*C.zxv_freight_ops_writer)(p)) })
	})
}

// PutLiteral appends a LITERAL op carrying b. ZXV_E_ARG for an empty b or more
// than is left of the declared total; ZXV_E_CAPACITY when the buffer is full.
func (w *FreightOpsWriter) PutLiteral(b []byte) error {
	return w.put(func(p *C.zxv_freight_ops_writer) C.int32_t {
		return C.zxv_freight_ops_put_literal(p, bytePtr(b), C.size_t(len(b)))
	})
}

// PutCopy appends a COPY op: n bytes starting distance bytes back (overlap
// allowed). ZXV_E_ARG for a distance of 0 or beyond what was produced.
func (w *FreightOpsWriter) PutCopy(distance, n uint64) error {
	return w.put(func(p *C.zxv_freight_ops_writer) C.int32_t {
		return C.zxv_freight_ops_put_copy(p, C.uint64_t(distance), C.uint64_t(n))
	})
}

// PutCIDRef appends a CIDREF op: n bytes the receiver resolves by content
// id (1..FreightCIDMax bytes).
func (w *FreightOpsWriter) PutCIDRef(cid []byte, n uint64) error {
	return w.put(func(p *C.zxv_freight_ops_writer) C.int32_t {
		return C.zxv_freight_ops_put_cidref(p, bytePtr(cid), C.size_t(len(cid)), C.uint64_t(n))
	})
}

// PutSeed appends a SEED op: n bytes of generator gen with params.
func (w *FreightOpsWriter) PutSeed(gen FreightGen, params []byte, n uint64) error {
	return w.put(func(p *C.zxv_freight_ops_writer) C.int32_t {
		return C.zxv_freight_ops_put_seed(p, C.uint8_t(gen), bytePtr(params), C.size_t(len(params)), C.uint64_t(n))
	})
}

// Finish returns the stream so far. ZXV_E_STATE while the ops produce fewer
// bytes than declared, or the sticky error of an earlier put. The writer
// stays usable: finish again after more puts.
func (w *FreightOpsWriter) Finish() ([]byte, error) {
	var out []byte
	err := w.h.use(func(p unsafe.Pointer) error {
		var e error
		out, e = readBytes(256, func(buf *C.uint8_t, cp C.size_t, n *C.size_t) C.int32_t {
			return C.zxv_freight_ops_writer_finish((*C.zxv_freight_ops_writer)(p), buf, cp, n)
		})
		return e
	})
	return out, err
}
