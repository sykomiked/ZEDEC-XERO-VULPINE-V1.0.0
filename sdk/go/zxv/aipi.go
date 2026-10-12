// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv

// Content ids and AIPI (ABI 1.5): IPFS CIDs and content-addressed
// functions.
//
// AIPI names data and functions by WHAT they are (a hash) instead of WHERE
// they live. A CID is a comparable value type holding the binary CID
// (version, codec, multihash); every function that takes one refuses bytes
// that are not exactly one canonical CIDv0 or CIDv1 with ErrCID. The CIDs
// are byte for byte the ones Kubo makes: ComputeCID is `ipfs block put`,
// UnixFSCID is `ipfs add --cid-version=1`, CID.String prints what Kubo
// prints.
//
// The four AIPI identifiers (all CIDv1, full sha2-256; the 0x3A10xx codecs
// are multicodec PRIVATE-USE values, not registered):
//
//   - data: codec raw 0x55 (or dag-pb 0x70): blobs, modules, contracts;
//   - function: codec AipiCodecFn + intent, the hash of a function record
//     (AipiFunction.Encode);
//   - call: codec AipiCodecCall, the hash of a call record (EncodeAipiCall);
//   - claim: not a CID, an executor's answer (AipiClaim.Encode).
//
// Records are decoded strictly (exact lengths, no trailing bytes; a
// function record must be canonical, so one function has exactly one CID).
//
// Honest limits:
//   - No network transport: no Vinea mesh, no IPFS peers, no HTTP gateway,
//     no libp2p. An AipiNode is an OFFLINE, in-process node: Get finds only
//     what was Put into this node.
//   - No EXECUTE: running a function needs the Z-Wasm sandbox
//     (sdk/kits/aipi), which is not part of libzxv. Plan reports the route a
//     call would take here; it runs nothing.
//   - No money moves in this group.
//   - A CID proves which bytes are meant. It does not prove that a claimed
//     output of a function is right, and it does not make bytes available.
//   - Claims are the bytes an executor signs; signing and checking the
//     signature are the caller's job.

/*
#include "zxv_api.h"
*/
import "C"

import (
	"encoding/hex"
	"runtime"
	"unsafe"
)

// CID sizes.
const (
	CIDMax       = int(C.ZXV_CID_MAX)        // longest binary CID
	CIDTextMax   = int(C.ZXV_CID_TEXT_MAX)   // longest CID text
	CIDDigestMax = int(C.ZXV_CID_DIGEST_MAX) // longest multihash digest (identity)
)

// Multicodec and multihash codes.
const (
	CIDCodecRaw     uint32 = C.ZXV_CID_CODEC_RAW     // plain bytes
	CIDCodecDagPB   uint32 = C.ZXV_CID_CODEC_DAG_PB  // UnixFS nodes
	CIDHashIdentity uint32 = C.ZXV_CID_HASH_IDENTITY // the bytes themselves
	CIDHashSHA256   uint32 = C.ZXV_CID_HASH_SHA2_256
	AipiCodecFn     uint32 = C.ZXV_AIPI_MC_FN            // zxv-fn (intent 0)
	AipiCodecTensor uint32 = C.ZXV_AIPI_MC_TENSOR_INFER  // zxv-tensor-infer (intent 1)
	AipiCodecSettle uint32 = C.ZXV_AIPI_MC_SETTLE_LEDGER // zxv-settle-ledger (intent 2)
	AipiCodecSwarm  uint32 = C.ZXV_AIPI_MC_SWARM_ROUTE   // zxv-swarm-route (intent 3)
	AipiCodecCall   uint32 = C.ZXV_AIPI_MC_CALL          // zxv-aipi-call
)

// CIDBase is a text form of a CID.
type CIDBase uint32

// CID text bases.
const (
	// CIDBaseDefault is Kubo's form: CIDv1 "b..." base32, CIDv0 "Qm...".
	CIDBaseDefault CIDBase = C.ZXV_CID_BASE_DEFAULT
	// CIDBase58BTC is "z" + base58btc (the AIPI form); CIDv1 only.
	CIDBase58BTC CIDBase = C.ZXV_CID_BASE_58BTC
)

// CID is one binary content id: a comparable value (use ==). The zero
// value is "no CID"; every function given one refuses it with ErrCID.
type CID struct {
	n uint8
	b [C.ZXV_CID_MAX]byte
}

// CIDInfo is the parts of a CID.
type CIDInfo struct {
	Version uint32 // 0 or 1
	Codec   uint32 // multicodec, e.g. CIDCodecRaw
	Hash    uint32 // multihash code, e.g. CIDHashSHA256
	Digest  []byte // the multihash digest
}

func (c *CID) ptr() *C.uint8_t {
	if c.n == 0 {
		return nil
	}
	return (*C.uint8_t)(unsafe.Pointer(&c.b[0]))
}

// outCID runs a call that writes a CID.
func outCID(f func(out *C.uint8_t, n *C.size_t) C.int32_t) (CID, error) {
	var c CID
	var n C.size_t
	if err := call(func() C.int32_t { return f((*C.uint8_t)(unsafe.Pointer(&c.b[0])), &n) }); err != nil {
		return CID{}, err
	}
	c.n = uint8(n)
	return c, nil
}

// CIDFromBytes checks that b is exactly one canonical binary CID and
// returns it (ErrCID otherwise).
func CIDFromBytes(b []byte) (CID, error) {
	if len(b) == 0 || len(b) > CIDMax {
		return CID{}, newError(StatusCID, "not a binary CID (length)")
	}
	if err := call(func() C.int32_t {
		return C.zxv_cid_info(bytePtr(b), C.size_t(len(b)), nil, nil, nil, nil, nil)
	}); err != nil {
		return CID{}, err
	}
	var c CID
	c.n = uint8(copy(c.b[:], b))
	return c, nil
}

// ComputeCID returns the CIDv1 of data as one block: sha2-256 under
// multicodec codec (CIDCodecRaw for plain bytes; Kubo `ipfs block put`).
func ComputeCID(codec uint32, data []byte) (CID, error) {
	return outCID(func(o *C.uint8_t, n *C.size_t) C.int32_t {
		return C.zxv_cid_compute(C.uint32_t(codec), bytePtr(data), C.size_t(len(data)), o, n)
	})
}

// UnixFSCID returns the root CID Kubo gives data as a file (`ipfs add
// --cid-version=1`): a raw CID up to 262144 bytes, a dag-pb root above. The
// blocks themselves are not kept.
func UnixFSCID(data []byte) (CID, error) {
	return outCID(func(o *C.uint8_t, n *C.size_t) C.int32_t {
		return C.zxv_cid_unixfs(bytePtr(data), C.size_t(len(data)), o, n)
	})
}

// ParseCID parses "b..." (CIDv1 base32), "Qm..." (CIDv0) or "z..." (CIDv1
// base58btc). Only the canonical text is accepted (ErrCID otherwise).
func ParseCID(text string) (CID, error) {
	cs, err := cstring(text)
	if err != nil {
		return CID{}, err
	}
	defer freeC(cs)
	return outCID(func(o *C.uint8_t, n *C.size_t) C.int32_t { return C.zxv_cid_from_text(cs, o, n) })
}

// MustParseCID is ParseCID for constants in tests and examples; it panics
// on a bad CID.
func MustParseCID(text string) CID {
	c, err := ParseCID(text)
	if err != nil {
		panic(err)
	}
	return c
}

// Bytes returns a copy of the binary CID.
func (c CID) Bytes() []byte { return append([]byte(nil), c.b[:c.n]...) }

// Len returns the binary length (0 for the zero value).
func (c CID) Len() int { return int(c.n) }

// IsZero reports whether c is the zero value (no CID).
func (c CID) IsZero() bool { return c.n == 0 }

// Text returns the CID as text in base.
func (c CID) Text(base CIDBase) (string, error) {
	return readString(func(b *C.char, cp C.size_t, l *C.size_t) C.int32_t {
		return C.zxv_cid_to_text(c.ptr(), C.size_t(c.n), C.uint32_t(base), b, cp, l)
	})
}

// String returns Kubo's text form ("bafy...", "Qm..."), or a hex dump in
// angle brackets when c is not a valid CID.
func (c CID) String() string {
	s, err := c.Text(CIDBaseDefault)
	if err != nil {
		return "<cid " + hex.EncodeToString(c.b[:c.n]) + ">"
	}
	return s
}

// Info returns the version, codec, hash code and digest.
func (c CID) Info() (CIDInfo, error) {
	var v, cd, h C.uint32_t
	var d [C.ZXV_CID_DIGEST_MAX]byte
	var dl C.size_t
	err := call(func() C.int32_t {
		return C.zxv_cid_info(c.ptr(), C.size_t(c.n), &v, &cd, &h, (*C.uint8_t)(unsafe.Pointer(&d[0])), &dl)
	})
	if err != nil {
		return CIDInfo{}, err
	}
	return CIDInfo{Version: uint32(v), Codec: uint32(cd), Hash: uint32(h), Digest: append([]byte(nil), d[:dl]...)}, nil
}

// ToV1 returns the CIDv1 form (dag-pb, same hash) of a CIDv0; a CIDv1 is
// returned unchanged.
func (c CID) ToV1() (CID, error) {
	return outCID(func(o *C.uint8_t, n *C.size_t) C.int32_t { return C.zxv_cid_to_v1(c.ptr(), C.size_t(c.n), o, n) })
}

// Verify returns nil if data is exactly the block c names (re-hashed;
// identity CIDs compare the bytes), else ErrCID. A UnixFS root names its
// root block, not the file: check a file with UnixFSCID instead.
func (c CID) Verify(data []byte) error {
	return call(func() C.int32_t {
		return C.zxv_cid_verify(c.ptr(), C.size_t(c.n), bytePtr(data), C.size_t(len(data)))
	})
}

// ProviderKey returns the Kademlia key under which providers of c are
// announced on Vinea: SHA-256("zxv/aipi/v1/provider" || binary CID).
func (c CID) ProviderKey() ([32]byte, error) {
	var k [32]byte
	err := call(func() C.int32_t { return C.zxv_aipi_provider_key(c.ptr(), C.size_t(c.n), ptr32(&k)) })
	return k, err
}

// ===== AIPI records =========================================================

// AipiIntent is what a function is for; it picks the function CID's codec.
type AipiIntent uint32

// Intents.
const (
	AipiIntentFn           AipiIntent = C.ZXV_AIPI_INTENT_FN
	AipiIntentTensorInfer  AipiIntent = C.ZXV_AIPI_INTENT_TENSOR_INFER
	AipiIntentSettleLedger AipiIntent = C.ZXV_AIPI_INTENT_SETTLE_LEDGER
	AipiIntentSwarmRoute   AipiIntent = C.ZXV_AIPI_INTENT_SWARM_ROUTE
)

// AipiVerify is how a function's result is accepted.
type AipiVerify uint32

// Verification modes.
const (
	AipiVerifyChecker AipiVerify = C.ZXV_AIPI_VERIFY_CHECKER // a checker export judges (input, output)
	AipiVerifyCert    AipiVerify = C.ZXV_AIPI_VERIFY_CERT    // as Checker, with a certificate
	AipiVerifyQuorum  AipiVerify = C.ZXV_AIPI_VERIFY_QUORUM  // k of n executors agree byte for byte
	AipiVerifyLocal   AipiVerify = C.ZXV_AIPI_VERIFY_LOCAL   // never accept a remote result
)

// AipiPrivacy says whether a function may leave the node.
type AipiPrivacy uint32

// Privacy values.
const (
	AipiPublic AipiPrivacy = C.ZXV_AIPI_PRIV_PUBLIC
	AipiLocal  AipiPrivacy = C.ZXV_AIPI_PRIV_LOCAL // private or key-bearing: never leaves the node
)

// AipiDeterminism says whether two runs give the same bytes.
type AipiDeterminism uint32

// Determinism values.
const (
	AipiNondeterministic AipiDeterminism = C.ZXV_AIPI_DET_NONDETERMINISTIC
	AipiDeterministic    AipiDeterminism = C.ZXV_AIPI_DET_DETERMINISTIC
)

// AIPI limits, in bytes or counts.
const (
	AipiNameMax   = int(C.ZXV_AIPI_NAME_MAX)   // export names: [A-Za-z0-9_]{1,63}
	AipiIOMax     = int(C.ZXV_AIPI_IO_MAX)     // call input, claim output
	AipiCertMax   = int(C.ZXV_AIPI_CERT_MAX)   // claim certificate
	AipiQuorumMax = int(C.ZXV_AIPI_QUORUM_MAX) // QUORUM n
	AipiFnMax     = int(C.ZXV_AIPI_FN_MAX)     // longest function record
	AipiCallMax   = int(C.ZXV_AIPI_CALL_MAX)   // longest call record
	AipiClaimMax  = int(C.ZXV_AIPI_CLAIM_MAX)  // longest claim record
	AipiBlockMax  = int(C.ZXV_AIPI_BLOCK_MAX)  // largest blob a node stores (2 MiB)
	AipiStoreMin  = uint64(C.ZXV_AIPI_STORE_MIN)
	AipiStoreMax  = uint64(C.ZXV_AIPI_STORE_MAX) // 256 MiB
	AipiBlocksMax = uint32(C.ZXV_AIPI_BLOCKS_MAX)
)

// AipiFunction is the content of a function record.
type AipiFunction struct {
	Intent      AipiIntent      // what the function is for
	Verify      AipiVerify      // how a result is checked
	Privacy     AipiPrivacy     // where it may run
	Determinism AipiDeterminism // whether runs must agree byte for byte
	// QuorumN and QuorumK are for AipiVerifyQuorum only (2 <= n <= 8,
	// n/2 < k <= n, deterministic); 0 otherwise.
	QuorumN, QuorumK uint32
	// FuelMax (> 0) is the most fuel one run may use.
	FuelMax uint64
	// Module is the CIDv1 raw sha2-256 of the Z-Wasm module bytes.
	Module CID
	// Contract is the CIDv1 raw sha2-256 of the z-contract text, or the
	// zero CID for none.
	Contract CID
	// Entry is the export computing the result.
	Entry string
	// Checker is the checking export (Checker and Cert modes; "" else).
	Checker string
	// Certifier is the certificate export (Cert mode only; "" else).
	Certifier string
}

func optCString(s string) (*C.char, error) {
	if s == "" {
		return nil, nil
	}
	return cstring(s)
}

// Encode returns the canonical function record. Every field is checked;
// none is masked (ErrUsage for a field out of range or breaking a rule,
// ErrCID for a CID that is not CIDv1 raw sha2-256).
func (f AipiFunction) Encode() ([]byte, error) {
	entry, err := cstring(f.Entry)
	if err != nil {
		return nil, err
	}
	defer freeC(entry)
	checker, err := optCString(f.Checker)
	if err != nil {
		return nil, err
	}
	defer freeC(checker)
	certifier, err := optCString(f.Certifier)
	if err != nil {
		return nil, err
	}
	defer freeC(certifier)
	mod, con := f.Module, f.Contract
	return readBytes(AipiFnMax, func(b *C.uint8_t, c C.size_t, l *C.size_t) C.int32_t {
		return C.zxv_aipi_fn_encode(C.uint32_t(f.Intent), C.uint32_t(f.Verify), C.uint32_t(f.Privacy),
			C.uint32_t(f.Determinism), C.uint32_t(f.QuorumN), C.uint32_t(f.QuorumK), C.uint64_t(f.FuelMax),
			mod.ptr(), C.size_t(mod.n), con.ptr(), C.size_t(con.n), entry, checker, certifier, b, c, l)
	})
}

// DecodeAipiFunction checks a function record strictly (ZXV_E_FORMAT for
// anything that is not a valid, canonical record) and returns its fields.
func DecodeAipiFunction(rec []byte) (AipiFunction, error) {
	var it, ve, pr, de, qn, qk C.uint32_t
	var fuel C.uint64_t
	if err := call(func() C.int32_t {
		return C.zxv_aipi_fn_decode(bytePtr(rec), C.size_t(len(rec)), &it, &ve, &pr, &de, &qn, &qk, &fuel)
	}); err != nil {
		return AipiFunction{}, err
	}
	f := AipiFunction{Intent: AipiIntent(it), Verify: AipiVerify(ve), Privacy: AipiPrivacy(pr),
		Determinism: AipiDeterminism(de), QuorumN: uint32(qn), QuorumK: uint32(qk), FuelMax: uint64(fuel)}
	var err error
	if f.Module, err = fnCID(rec, C.ZXV_AIPI_FIELD_MODULE); err != nil {
		return AipiFunction{}, err
	}
	if f.Contract, err = fnCID(rec, C.ZXV_AIPI_FIELD_CONTRACT); err != nil && statusOf(err) != StatusNotFound {
		return AipiFunction{}, err
	}
	names := []*string{&f.Entry, &f.Checker, &f.Certifier}
	fields := []C.uint32_t{C.ZXV_AIPI_FIELD_ENTRY, C.ZXV_AIPI_FIELD_CHECKER, C.ZXV_AIPI_FIELD_CERTIFIER}
	for i, fld := range fields {
		if *names[i], err = readString(func(b *C.char, c C.size_t, l *C.size_t) C.int32_t {
			return C.zxv_aipi_fn_get_name(bytePtr(rec), C.size_t(len(rec)), fld, b, c, l)
		}); err != nil {
			return AipiFunction{}, err
		}
	}
	return f, nil
}

func fnCID(rec []byte, field C.uint32_t) (CID, error) {
	return outCID(func(o *C.uint8_t, n *C.size_t) C.int32_t {
		return C.zxv_aipi_fn_get_cid(bytePtr(rec), C.size_t(len(rec)), field, o, n)
	})
}

// statusOf returns the status of an error from this package (0 for nil).
func statusOf(err error) int32 {
	if err == nil {
		return 0
	}
	var e *Error
	for x := err; x != nil; {
		if ee, ok := x.(*Error); ok {
			e = ee
			break
		}
		u, ok := x.(interface{ Unwrap() error })
		if !ok {
			break
		}
		x = u.Unwrap()
	}
	if e == nil {
		return StatusInternal
	}
	return e.Status
}

// AipiFunctionCID returns the function CID of a record: codec AipiCodecFn
// + intent, sha2-256 of the record bytes.
func AipiFunctionCID(rec []byte) (CID, error) {
	return outCID(func(o *C.uint8_t, n *C.size_t) C.int32_t {
		return C.zxv_aipi_fn_cid(bytePtr(rec), C.size_t(len(rec)), o, n)
	})
}

// EncodeAipiCall returns the call record naming one invocation: a function
// CID and its input (at most AipiIOMax bytes).
func EncodeAipiCall(fn CID, input []byte) ([]byte, error) {
	return readBytes(64+len(input), func(b *C.uint8_t, c C.size_t, l *C.size_t) C.int32_t {
		return C.zxv_aipi_call_encode(fn.ptr(), C.size_t(fn.n), bytePtr(input), C.size_t(len(input)), b, c, l)
	})
}

// DecodeAipiCall checks a call record strictly and returns its function
// CID and a copy of its input.
func DecodeAipiCall(rec []byte) (fn CID, input []byte, err error) {
	var fo, fl, io, il C.size_t
	if err = call(func() C.int32_t {
		return C.zxv_aipi_call_decode(bytePtr(rec), C.size_t(len(rec)), &fo, &fl, &io, &il)
	}); err != nil {
		return CID{}, nil, err
	}
	if fn, err = CIDFromBytes(rec[fo : fo+fl]); err != nil {
		return CID{}, nil, err
	}
	return fn, append([]byte{}, rec[io:io+il]...), nil
}

// AipiCallCID returns the call CID (codec AipiCodecCall, sha2-256 of the
// call record) of (fn, input).
func AipiCallCID(fn CID, input []byte) (CID, error) {
	return outCID(func(o *C.uint8_t, n *C.size_t) C.int32_t {
		return C.zxv_aipi_call_cid(fn.ptr(), C.size_t(fn.n), bytePtr(input), C.size_t(len(input)), o, n)
	})
}

// AipiClaim is an executor's answer to a call: the bytes it signs.
type AipiClaim struct {
	Call     CID      // the call CID answered
	Executor [32]byte // the executor's node id
	Output   []byte   // the answer (at most AipiIOMax bytes)
	Cert     []byte   // the certificate, for Cert functions (at most AipiCertMax)
}

// Encode returns the claim record. Signing it is the caller's job.
func (c AipiClaim) Encode() ([]byte, error) {
	call := c.Call
	return readBytes(128+len(c.Output)+len(c.Cert), func(b *C.uint8_t, cp C.size_t, l *C.size_t) C.int32_t {
		return C.zxv_aipi_claim_encode(call.ptr(), C.size_t(call.n), ptr32(&c.Executor), bytePtr(c.Output),
			C.size_t(len(c.Output)), bytePtr(c.Cert), C.size_t(len(c.Cert)), b, cp, l)
	})
}

// DecodeAipiClaim checks a claim record strictly and returns its parts
// (copies).
func DecodeAipiClaim(rec []byte) (AipiClaim, error) {
	var co, cl, eo, oo, ol, xo, xl C.size_t
	if err := call(func() C.int32_t {
		return C.zxv_aipi_claim_decode(bytePtr(rec), C.size_t(len(rec)), &co, &cl, &eo, &oo, &ol, &xo, &xl)
	}); err != nil {
		return AipiClaim{}, err
	}
	callID, err := CIDFromBytes(rec[co : co+cl])
	if err != nil {
		return AipiClaim{}, err
	}
	c := AipiClaim{Call: callID, Output: append([]byte{}, rec[oo:oo+ol]...), Cert: append([]byte{}, rec[xo:xo+xl]...)}
	copy(c.Executor[:], rec[eo:eo+32])
	return c, nil
}

// ===== the AIPI node ========================================================

// AipiRoute is the route a call would take (AipiNode.Plan).
type AipiRoute int32

// Routes. This library is offline, so Plan reports only AipiRouteLocal or
// AipiRouteUnavailable; the others are the kernel's routes for a networked
// host, listed so the values are fixed.
const (
	AipiRouteCache       AipiRoute = C.ZXV_AIPI_ROUTE_CACHE
	AipiRouteLocal       AipiRoute = C.ZXV_AIPI_ROUTE_LOCAL
	AipiRouteFetchLocal  AipiRoute = C.ZXV_AIPI_ROUTE_FETCH_LOCAL
	AipiRouteMesh        AipiRoute = C.ZXV_AIPI_ROUTE_MESH
	AipiRouteUnavailable AipiRoute = C.ZXV_AIPI_ROUTE_UNAVAILABLE
)

// String returns the route name, e.g. "LOCAL".
func (r AipiRoute) String() string {
	names := []string{"", "CACHE", "LOCAL", "FETCH_LOCAL", "MESH", "UNAVAILABLE"}
	if r > 0 && int(r) < len(names) {
		return names[r]
	}
	return "AipiRoute(?)"
}

// AipiNodeInfo is a node's audit counters.
type AipiNodeInfo struct {
	Blocks       uint32 // blocks indexed
	StoreUsed    uint64 // store bytes used
	GetsVerified uint32 // GETs whose bytes re-hashed correctly
	Forbidden    uint32 // GETs whose stored bytes failed their hash (dropped)
}

// AipiNode is an offline, in-memory IPFS node (block store and pins) with
// the AIPI context over it (zxv_aipi). It belongs to the context of the
// capability that created it and fails closed with it. Calls that store
// (Put, PublishFunction) use that capability, which needs CapAIPI; reads
// need none. Safe for concurrent use. Close it before its capability.
type AipiNode struct{ ctxHandle }

// NewAipiNode creates a node holding at most maxBlocks blocks (1 ..
// AipiBlocksMax) in storeBytes of memory (AipiStoreMin .. AipiStoreMax;
// each block costs its length plus about 100 bytes). nodeKey (copied; may
// be nil) seals private blocks with ChaCha20-Poly1305; without it, private
// puts are refused with ZXV_E_STATE. cap needs CapAIPI.
func NewAipiNode(cap *Capability, storeBytes uint64, maxBlocks uint32, nodeKey *[32]byte) (*AipiNode, error) {
	var kp *C.uint8_t
	if nodeKey != nil {
		kp = ptr32(nodeKey)
	}
	var r *C.zxv_aipi
	if err := cap.gated(func(p *C.zxv_cap) C.int32_t {
		return C.zxv_aipi_create(p, C.size_t(storeBytes), C.uint32_t(maxBlocks), kp, &r)
	}); err != nil {
		return nil, err
	}
	n := &AipiNode{newCtxHandle("aipi node", unsafe.Pointer(r), func(p unsafe.Pointer) { C.zxv_aipi_destroy((*C.zxv_aipi)(p)) }, cap, false)}
	runtime.SetFinalizer(n, func(n *AipiNode) { _ = n.h.close() })
	return n, nil
}

// Close destroys the node and wipes its key.
func (n *AipiNode) Close() error { return n.h.close() }

// Put stores and pins data (at most AipiBlockMax bytes) as one raw block
// and returns its CID. private stores it sealed and never announceable.
// Putting the same bytes again is a no-op; the same bytes public after
// private (or the reverse) is ZXV_E_STATE: making something public is a
// deliberate act, not a side effect. ZXV_E_CAPACITY when the store is full.
func (n *AipiNode) Put(data []byte, private bool) (CID, error) {
	var c CID
	var l C.size_t
	err := n.gated(func(cp *C.zxv_cap, p unsafe.Pointer) C.int32_t {
		return C.zxv_aipi_put(cp, (*C.zxv_aipi)(p), bytePtr(data), C.size_t(len(data)), boolInt(private),
			(*C.uint8_t)(unsafe.Pointer(&c.b[0])), &l)
	})
	c.n = uint8(l)
	if err != nil {
		return CID{}, err
	}
	return c, nil
}

// Get returns the bytes cid names, re-hashed before they are returned.
// ErrNotFound when the block is not held here (there is no network); ErrCID
// for a bad or identity CID, or stored bytes that no longer match (those
// are dropped and counted as forbidden); ZXV_E_STATE for a private block
// and no node key.
func (n *AipiNode) Get(cid CID) ([]byte, error) {
	var out []byte
	err := n.localSeq(func(p unsafe.Pointer) error {
		var e error
		out, e = readBytes(256, func(b *C.uint8_t, c C.size_t, l *C.size_t) C.int32_t {
			return C.zxv_aipi_get((*C.zxv_aipi)(p), cid.ptr(), C.size_t(cid.n), b, c, l)
		})
		return e
	})
	return out, err
}

// PublishFunction stores a function record (checked strictly; sealed when
// its privacy is AipiLocal) and returns its function CID. The module and
// contract need not be held yet.
func (n *AipiNode) PublishFunction(rec []byte) (CID, error) {
	var c CID
	var l C.size_t
	err := n.gated(func(cp *C.zxv_cap, p unsafe.Pointer) C.int32_t {
		return C.zxv_aipi_publish_fn(cp, (*C.zxv_aipi)(p), bytePtr(rec), C.size_t(len(rec)),
			(*C.uint8_t)(unsafe.Pointer(&c.b[0])), &l)
	})
	c.n = uint8(l)
	if err != nil {
		return CID{}, err
	}
	return c, nil
}

// LoadFunction returns the function record fn names: fetched, verified by
// hash, decoded strictly, and its codec checked against its intent.
func (n *AipiNode) LoadFunction(fn CID) ([]byte, error) {
	var out []byte
	err := n.localSeq(func(p unsafe.Pointer) error {
		var e error
		out, e = readBytes(AipiFnMax, func(b *C.uint8_t, c C.size_t, l *C.size_t) C.int32_t {
			return C.zxv_aipi_load_fn((*C.zxv_aipi)(p), fn.ptr(), C.size_t(fn.n), b, c, l)
		})
		return e
	})
	return out, err
}

// Plan returns the route a call of fn on input would take now. It runs and
// changes nothing. In this offline library: AipiRouteLocal when the module
// (and contract) are held, else AipiRouteUnavailable.
func (n *AipiNode) Plan(fn CID, input []byte, inputPrivate bool) (AipiRoute, error) {
	var r C.int32_t
	err := n.local(func(p unsafe.Pointer) C.int32_t {
		return C.zxv_aipi_plan((*C.zxv_aipi)(p), fn.ptr(), C.size_t(fn.n), bytePtr(input), C.size_t(len(input)),
			boolInt(inputPrivate), &r)
	})
	return AipiRoute(r), err
}

// Info returns the audit counters.
func (n *AipiNode) Info() (AipiNodeInfo, error) {
	var b, g, f C.uint32_t
	var u C.uint64_t
	err := n.local(func(p unsafe.Pointer) C.int32_t { return C.zxv_aipi_info((*C.zxv_aipi)(p), &b, &u, &g, &f) })
	return AipiNodeInfo{Blocks: uint32(b), StoreUsed: uint64(u), GetsVerified: uint32(g), Forbidden: uint32(f)}, err
}
