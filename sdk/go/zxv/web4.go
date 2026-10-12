// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv

// The Web 4 agent layer (ABI 1.5): agents with ML-DSA-65 keys, their signed
// tool manifests, a directory of verified agent cards, human consent tokens
// and signed envelopes.
//
// IDENTITY. agent_id = SHA3-256(agent public key). An agent is bound to the
// node hosting it by a binding signed by the node's key (peer_id = SHA3-256
// of the node key); a self-bound agent signs its own binding and has
// peer_id == agent_id.
//
// CONSENT. An agent never moves money or shares personal data without a
// token signed by the HUMAN's key over {human, agent, action digest, scope,
// max amount, validity, nonce}. The action digest binds the exact tool,
// parties, request id and payload. Tokens are single-use: a W4ConsentLog
// records consumed and revoked tokens.
//
// SECRETS. A private key never leaves its W4Agent handle: there is no call
// that returns it. Close wipes it. This package never logs keys.
//
// CAPABILITIES. Creating a W4Agent and everything it signs (bindings,
// manifests, envelopes) needs CapCryptoSign; issuing a consent token needs
// CapAttest. Parsing, verifying, digests, manifests, the directory and the
// consent log need no capability.
//
// TIME. There is no clock in here: every call that needs the time takes
// nowMs (Unix milliseconds) from the caller.
//
// Honest limits: envelopes are signed, not encrypted. A consent token
// proves that a key holder signed; whether that holder is the right human
// and understood what they signed depends on the device and UI that showed
// the action, which this library cannot see. No network transport, no
// replay window or rate limiting on the receiving side, and no Web 2 or Web
// 3 bridges. Interoperability is between ZXV instances only; no other agent
// protocol is implemented or claimed. ML-DSA-65 here is not FIPS 140-3
// validated.

/*
#include <stdlib.h>
#include "zxv_api.h"
*/
import "C"

import (
	"runtime"
	"unsafe"
)

// Web 4 sizes and limits.
const (
	W4PKBytes       = int(C.ZXV_W4_PK_BYTES)           // 1952: ML-DSA-65 public key
	W4SigBytes      = int(C.ZXV_W4_SIG_BYTES)          // 3309: ML-DSA-65 signature
	W4IDBytes       = int(C.ZXV_W4_ID_BYTES)           // 32: agent, peer and human ids
	W4NameMax       = int(C.ZXV_W4_NAME_MAX)           // 31 characters of [a-z0-9._-]
	W4MaxTools      = int(C.ZXV_W4_MAX_TOOLS)          // 16 tools per manifest
	W4MsgMaxPayload = int(C.ZXV_W4_MSG_MAX_PAYLOAD)    // 2048 payload bytes per envelope
	W4ConsentBytes  = int(C.ZXV_W4_CONSENT_BYTES)      // 3453: an encoded consent token
	W4CardMax       = int(C.ZXV_W4_CARD_MAX)           // 7259: a node-bound card
	W4ManifestMax   = int(C.ZXV_W4_MANIFEST_MAX)       // 4349: an encoded manifest
	W4EnvMax        = int(C.ZXV_W4_ENV_MAX)            // 8970: an encoded envelope
	W4DirMax        = uint32(C.ZXV_W4_DIR_MAX)         // 4096 directory entries
	W4ConsentLogMax = uint32(C.ZXV_W4_CONSENT_LOG_MAX) // 65536 consent log entries
)

// Tool flags (W4Tool.Flags).
const (
	W4ToolMoney  uint32 = C.ZXV_W4_TOOL_MONEY  // the tool moves money
	W4ToolPII    uint32 = C.ZXV_W4_TOOL_PII    // the tool reveals personal data
	W4ToolStream uint32 = C.ZXV_W4_TOOL_STREAM // answers with STREAM chunks
)

// Consent scopes.
const (
	W4ConsentMoney uint32 = C.ZXV_W4_CONSENT_MONEY // may move money up to MaxVFV
	W4ConsentPII   uint32 = C.ZXV_W4_CONSENT_PII   // may share personal data
)

// W4MsgKind is an envelope kind.
type W4MsgKind int32

// Envelope kinds.
const (
	W4MsgRequest  W4MsgKind = C.ZXV_W4_MSG_REQUEST
	W4MsgResponse W4MsgKind = C.ZXV_W4_MSG_RESPONSE
	W4MsgStream   W4MsgKind = C.ZXV_W4_MSG_STREAM
	W4MsgCancel   W4MsgKind = C.ZXV_W4_MSG_CANCEL
	W4MsgError    W4MsgKind = C.ZXV_W4_MSG_ERROR
)

// String returns the kind's name, e.g. "REQUEST".
func (k W4MsgKind) String() string {
	names := []string{"", "REQUEST", "RESPONSE", "STREAM", "CANCEL", "ERROR"}
	if k > 0 && int(k) < len(names) {
		return names[k]
	}
	return "W4MsgKind(?)"
}

// Envelope flags.
const (
	W4FlagFinal   uint32 = C.ZXV_W4_MF_FINAL   // last STREAM chunk / final response
	W4FlagConsent uint32 = C.ZXV_W4_MF_CONSENT // a token is attached (set by sealing only)
)

// W4Agent is a key handle: an ML-DSA-65 key pair and the agent's signed
// card. A human's key is a W4Agent too (human_id is its AgentID). The
// private key never leaves the handle and Close wipes it. It belongs to
// the context of the capability that created it, signs with that
// capability, and fails closed with that context. Calls on one agent are
// serialized (sealing advances its envelope sequence). Close it before its
// capability.
type W4Agent struct{ ctxHandle }

// NewW4Agent creates a key handle named name (1..31 of [a-z0-9._-]). seed
// gives a reproducible key, or nil for a fresh one from the OS. node is
// the agent hosting this one (it signs the binding; it must belong to the
// same context and may be closed afterwards), or nil for a self-bound
// agent. nowMs is the binding time. cap needs CapCryptoSign.
func NewW4Agent(cap *Capability, seed *[32]byte, name string, node *W4Agent, nowMs uint64) (*W4Agent, error) {
	cn, err := cstring(name)
	if err != nil {
		return nil, err
	}
	defer freeC(cn)
	var sp *C.uint8_t
	var sd [32]byte
	if seed != nil {
		sd = *seed
		sp = ptr32(&sd)
		defer wipe(sd[:])
	}
	var r *C.zxv_w4_agent
	create := func(np *C.zxv_w4_agent) error {
		return cap.gated(func(cp *C.zxv_cap) C.int32_t {
			return C.zxv_w4_agent_create(cp, sp, cn, np, C.uint64_t(nowMs), &r)
		})
	}
	if node != nil {
		if err := sameContext(cap, node.cap, "node"); err != nil {
			return nil, err
		}
		err = node.h.use(func(p unsafe.Pointer) error { return create((*C.zxv_w4_agent)(p)) })
	} else {
		err = create(nil)
	}
	if err != nil {
		return nil, err
	}
	a := &W4Agent{newCtxHandle("web4 agent", unsafe.Pointer(r), func(p unsafe.Pointer) { C.zxv_w4_agent_destroy((*C.zxv_w4_agent)(p)) }, cap, true)}
	runtime.SetFinalizer(a, func(a *W4Agent) { _ = a.h.close() })
	return a, nil
}

// Close wipes the private key and frees the handle. Closing twice is a no-op.
func (a *W4Agent) Close() error { return a.h.close() }

// W4AgentInfo is an agent's public facts.
type W4AgentInfo struct {
	AgentID   [32]byte // SHA3-256 of PublicKey
	PeerID    [32]byte // the hosting node's id (== AgentID when self-bound)
	PublicKey []byte   // ML-DSA-65 public key, W4PKBytes
}

// Info returns the agent's ids and public key.
func (a *W4Agent) Info() (W4AgentInfo, error) {
	var i W4AgentInfo
	pk := make([]byte, W4PKBytes)
	err := a.local(func(p unsafe.Pointer) C.int32_t {
		return C.zxv_w4_agent_info((*C.zxv_w4_agent)(p), ptr32(&i.AgentID), ptr32(&i.PeerID), bytePtr(pk))
	})
	if err != nil {
		return W4AgentInfo{}, err
	}
	i.PublicKey = pk
	return i, nil
}

// Card returns the agent's card: what a peer needs to verify it (libzxv's
// "W4CD" format; at most W4CardMax bytes).
func (a *W4Agent) Card() ([]byte, error) {
	var out []byte
	err := a.localSeq(func(p unsafe.Pointer) error {
		var e error
		out, e = readBytes(W4CardMax, func(b *C.uint8_t, c C.size_t, l *C.size_t) C.int32_t {
			return C.zxv_w4_agent_card((*C.zxv_w4_agent)(p), b, c, l)
		})
		return e
	})
	return out, err
}

// W4CardInfo is what a verified card says.
type W4CardInfo struct {
	AgentID [32]byte // derived from the agent key in the card
	PeerID  [32]byte // derived from the node key (or the agent key when self-bound)
	Name    string   // the agent's name
	BoundMs uint64   // when the binding was signed
}

// VerifyW4Card checks that card is well formed and its binding signature
// valid. ZXV_E_FORMAT: malformed. ErrVerificationFailed: the signature fails. Pure.
func VerifyW4Card(card []byte) (W4CardInfo, error) {
	var i W4CardInfo
	var name [32]C.char
	var ms C.uint64_t
	err := call(func() C.int32_t {
		return C.zxv_w4_card_verify(bytePtr(card), C.size_t(len(card)), ptr32(&i.AgentID), ptr32(&i.PeerID), &name[0], &ms)
	})
	if err != nil {
		return W4CardInfo{}, err
	}
	i.Name = C.GoString(&name[0])
	i.BoundMs = uint64(ms)
	return i, nil
}

// W4Directory holds verified agent cards keyed by agent id. Calls on one
// directory are serialized. Close it when done.
type W4Directory struct{ h *handle }

// NewW4Directory creates a directory of capacity entries (1..W4DirMax).
func NewW4Directory(capacity uint32) (*W4Directory, error) {
	var r *C.zxv_w4_dir
	if err := call(func() C.int32_t { return C.zxv_w4_dir_create(C.uint32_t(capacity), &r) }); err != nil {
		return nil, err
	}
	d := &W4Directory{newHandle("web4 directory", unsafe.Pointer(r), func(p unsafe.Pointer) { C.zxv_w4_dir_destroy((*C.zxv_w4_dir)(p)) })}
	d.h.exclusive = true
	runtime.SetFinalizer(d, func(d *W4Directory) { _ = d.h.close() })
	return d, nil
}

// Close frees the directory. Closing twice is a no-op.
func (d *W4Directory) Close() error { return d.h.close() }

// Add verifies card, then adds it or replaces the entry for the same
// agent. ZXV_E_FORMAT, ErrVerificationFailed as VerifyW4Card; ZXV_E_STATE when the card's
// binding is older than the one held (no rollback); ZXV_E_CAPACITY when full.
func (d *W4Directory) Add(card []byte) error {
	return d.h.use(func(p unsafe.Pointer) error {
		return call(func() C.int32_t { return C.zxv_w4_dir_add((*C.zxv_w4_dir)(p), bytePtr(card), C.size_t(len(card))) })
	})
}

// Find returns the card held for agentID. ErrNotFound if none.
func (d *W4Directory) Find(agentID [32]byte) ([]byte, error) {
	var out []byte
	err := d.h.use(func(p unsafe.Pointer) error {
		var e error
		out, e = readBytes(W4CardMax, func(b *C.uint8_t, c C.size_t, l *C.size_t) C.int32_t {
			return C.zxv_w4_dir_find((*C.zxv_w4_dir)(p), ptr32(&agentID), b, c, l)
		})
		return e
	})
	return out, err
}

// Count returns how many cards the directory holds.
func (d *W4Directory) Count() (uint32, error) {
	var n C.uint32_t
	err := d.h.use(func(p unsafe.Pointer) error {
		return call(func() C.int32_t { return C.zxv_w4_dir_count((*C.zxv_w4_dir)(p), &n) })
	})
	return uint32(n), err
}

// W4Tool is one entry of a tool manifest.
type W4Tool struct {
	Name       string // 1..31 of [a-z0-9._-]
	PriceVFV   uint64 // price in VFV minor units (2 digits)
	RatePerMin uint32 // calls per minute, >= 1
	Burst      uint32 // burst, >= 1
	Flags      uint32 // W4Tool* bits
	MaxInput   uint32 // largest input payload, <= W4MsgMaxPayload
}

// W4ManifestInfo is a manifest's header.
type W4ManifestInfo struct {
	AgentID   [32]byte // zero until signed
	PeerID    [32]byte // zero until signed
	Version   uint32   // the manifest version
	IssuedMs  uint64   // valid from (Unix ms)
	ExpiresMs uint64   // valid until, exclusive (Unix ms)
	ToolCount uint32   // tools listed
	Signed    bool     // whether it carries a signature (not checked here)
}

// W4Manifest is a tool manifest: up to 16 tools, valid in [issued,
// expires), signed by its agent. Adding a tool after signing drops the
// signature. Calls on one manifest are serialized. Close it when done.
type W4Manifest struct{ h *handle }

func newW4Manifest(r *C.zxv_w4_manifest) *W4Manifest {
	m := &W4Manifest{newHandle("web4 manifest", unsafe.Pointer(r), func(p unsafe.Pointer) { C.zxv_w4_manifest_destroy((*C.zxv_w4_manifest)(p)) })}
	m.h.exclusive = true
	runtime.SetFinalizer(m, func(m *W4Manifest) { _ = m.h.close() })
	return m
}

// NewW4Manifest creates an empty, unsigned manifest valid in [issuedMs,
// expiresMs). ZXV_E_ARG when expiresMs <= issuedMs.
func NewW4Manifest(version uint32, issuedMs, expiresMs uint64) (*W4Manifest, error) {
	var r *C.zxv_w4_manifest
	if err := call(func() C.int32_t {
		return C.zxv_w4_manifest_create(C.uint32_t(version), C.uint64_t(issuedMs), C.uint64_t(expiresMs), &r)
	}); err != nil {
		return nil, err
	}
	return newW4Manifest(r), nil
}

// DecodeW4Manifest parses a manifest's wire form. The signature is kept but
// NOT checked: call Verify. ZXV_E_FORMAT: malformed.
func DecodeW4Manifest(b []byte) (*W4Manifest, error) {
	var r *C.zxv_w4_manifest
	if err := call(func() C.int32_t { return C.zxv_w4_manifest_decode(bytePtr(b), C.size_t(len(b)), &r) }); err != nil {
		return nil, err
	}
	return newW4Manifest(r), nil
}

// Close frees the manifest. Closing twice is a no-op.
func (m *W4Manifest) Close() error { return m.h.close() }

func (m *W4Manifest) do(f func(p *C.zxv_w4_manifest) C.int32_t) error {
	return m.h.use(func(p unsafe.Pointer) error {
		return call(func() C.int32_t { return f((*C.zxv_w4_manifest)(p)) })
	})
}

// AddTool adds a tool. ZXV_E_ARG, ZXV_E_TOO_LONG (name), ZXV_E_DUPLICATE (name
// already listed), ZXV_E_CAPACITY (16 tools).
func (m *W4Manifest) AddTool(t W4Tool) error {
	cn, err := cstring(t.Name)
	if err != nil {
		return err
	}
	defer freeC(cn)
	return m.do(func(p *C.zxv_w4_manifest) C.int32_t {
		return C.zxv_w4_manifest_add_tool(p, cn, C.uint64_t(t.PriceVFV), C.uint32_t(t.RatePerMin), C.uint32_t(t.Burst),
			C.uint32_t(t.Flags), C.uint32_t(t.MaxInput))
	})
}

// Sign signs the manifest with agent's key, using the agent's capability
// (needs CapCryptoSign).
func (m *W4Manifest) Sign(agent *W4Agent) error {
	return m.h.use(func(mp unsafe.Pointer) error {
		return agent.gated(func(cp *C.zxv_cap, ap unsafe.Pointer) C.int32_t {
			return C.zxv_w4_manifest_sign(cp, (*C.zxv_w4_agent)(ap), (*C.zxv_w4_manifest)(mp))
		})
	})
}

// Verify returns nil if the manifest is signed by publicKey, that key is
// the manifest's agent, and it is valid at nowMs. ErrVerificationFailed: wrong key or
// a bad (or no) signature. ZXV_E_STATE: a valid signature not valid at nowMs.
func (m *W4Manifest) Verify(publicKey []byte, nowMs uint64) error {
	if err := need(publicKey, W4PKBytes, "public key"); err != nil {
		return err
	}
	return m.do(func(p *C.zxv_w4_manifest) C.int32_t {
		return C.zxv_w4_manifest_verify(p, bytePtr(publicKey), C.uint64_t(nowMs))
	})
}

// Encode returns the signed wire form. ZXV_E_STATE if not signed.
func (m *W4Manifest) Encode() ([]byte, error) {
	var out []byte
	err := m.h.use(func(p unsafe.Pointer) error {
		var e error
		out, e = readBytes(W4ManifestMax, func(b *C.uint8_t, c C.size_t, l *C.size_t) C.int32_t {
			return C.zxv_w4_manifest_encode((*C.zxv_w4_manifest)(p), b, c, l)
		})
		return e
	})
	return out, err
}

// JSON returns the Web 2 discovery face (e.g. /.well-known/web4-agent.json).
// Prices stay integers in minor units.
func (m *W4Manifest) JSON() (string, error) {
	var out string
	err := m.h.use(func(p unsafe.Pointer) error {
		var e error
		out, e = readString(func(b *C.char, c C.size_t, l *C.size_t) C.int32_t {
			return C.zxv_w4_manifest_to_json((*C.zxv_w4_manifest)(p), b, c, l)
		})
		return e
	})
	return out, err
}

// Info returns the manifest's header.
func (m *W4Manifest) Info() (W4ManifestInfo, error) {
	var i W4ManifestInfo
	var ver, n C.uint32_t
	var is, ex C.uint64_t
	var sg C.int32_t
	err := m.do(func(p *C.zxv_w4_manifest) C.int32_t {
		return C.zxv_w4_manifest_info(p, ptr32(&i.AgentID), ptr32(&i.PeerID), &ver, &is, &ex, &n, &sg)
	})
	if err != nil {
		return W4ManifestInfo{}, err
	}
	i.Version, i.IssuedMs, i.ExpiresMs, i.ToolCount, i.Signed = uint32(ver), uint64(is), uint64(ex), uint32(n), sg != 0
	return i, nil
}

// Tool returns tool index (0-based). ErrNotFound past the last tool.
func (m *W4Manifest) Tool(index uint32) (W4Tool, error) {
	var name [32]C.char
	var price C.uint64_t
	var rate, burst, flags, maxIn C.uint32_t
	err := m.do(func(p *C.zxv_w4_manifest) C.int32_t {
		return C.zxv_w4_manifest_tool(p, C.uint32_t(index), &name[0], &price, &rate, &burst, &flags, &maxIn)
	})
	if err != nil {
		return W4Tool{}, err
	}
	return W4Tool{C.GoString(&name[0]), uint64(price), uint32(rate), uint32(burst), uint32(flags), uint32(maxIn)}, nil
}

// Tools returns every tool, in order.
func (m *W4Manifest) Tools() ([]W4Tool, error) {
	info, err := m.Info()
	if err != nil {
		return nil, err
	}
	out := make([]W4Tool, 0, info.ToolCount)
	for i := uint32(0); i < info.ToolCount; i++ {
		t, err := m.Tool(i)
		if err != nil {
			return nil, err
		}
		out = append(out, t)
	}
	return out, nil
}

// Find returns the index of the tool called name. ErrNotFound if not listed.
func (m *W4Manifest) Find(name string) (uint32, error) {
	cn, err := cstring(name)
	if err != nil {
		return 0, err
	}
	defer freeC(cn)
	var i C.uint32_t
	err = m.do(func(p *C.zxv_w4_manifest) C.int32_t { return C.zxv_w4_manifest_find(p, cn, &i) })
	return uint32(i), err
}

// W4ActionDigest returns the digest a consent token for one request must
// carry: SHA3-256 over the tool, from, to, request id and payload. Pure.
// ZXV_E_TOO_LONG: payload over W4MsgMaxPayload.
func W4ActionDigest(tool string, from, to [32]byte, reqID uint64, payload []byte) ([32]byte, error) {
	var d [32]byte
	cn, err := cstring(tool)
	if err != nil {
		return d, err
	}
	defer freeC(cn)
	err = call(func() C.int32_t {
		return C.zxv_w4_action_digest(cn, ptr32(&from), ptr32(&to), C.uint64_t(reqID), bytePtr(payload), C.size_t(len(payload)), ptr32(&d))
	})
	return d, err
}

// W4ConsentToken is an encoded consent token ("W4CS", W4ConsentBytes). It
// is a value type; it carries a signature, not a secret.
type W4ConsentToken [3453]byte

func (t *W4ConsentToken) ptr() *C.uint8_t { return (*C.uint8_t)(unsafe.Pointer(&t[0])) }

// W4ConsentInfo is a token's fields.
type W4ConsentInfo struct {
	HumanID   [32]byte // the signing human's id
	AgentID   [32]byte // the agent allowed to act
	Action    [32]byte // the action digest
	Scope     uint32   // W4Consent* bits
	MaxVFV    uint64   // money ceiling, VFV minor units
	IssuedMs  uint64   // valid from (Unix ms)
	ExpiresMs uint64   // valid until, exclusive (Unix ms)
	Nonce     [16]byte // makes the token single-use
}

// Info reads the token's fields without checking its signature.
// ZXV_E_FORMAT: malformed.
func (t *W4ConsentToken) Info() (W4ConsentInfo, error) {
	var i W4ConsentInfo
	var sc C.uint32_t
	var mx, is, ex C.uint64_t
	err := call(func() C.int32_t {
		return C.zxv_w4_consent_info(t.ptr(), ptr32(&i.HumanID), ptr32(&i.AgentID), ptr32(&i.Action), &sc, &mx, &is, &ex, ptr16(&i.Nonce))
	})
	if err != nil {
		return W4ConsentInfo{}, err
	}
	i.Scope, i.MaxVFV, i.IssuedMs, i.ExpiresMs = uint32(sc), uint64(mx), uint64(is), uint64(ex)
	return i, nil
}

// W4ConsentRequest is what a human consents to.
type W4ConsentRequest struct {
	AgentID   [32]byte  // the agent allowed to act
	Action    [32]byte  // W4ActionDigest of the exact request
	Scope     uint32    // nonzero W4Consent* bits
	MaxVFV    uint64    // money ceiling, VFV minor units
	IssuedMs  uint64    // valid from
	ExpiresMs uint64    // valid until (exclusive), > IssuedMs
	Nonce     *[16]byte // makes the token single-use; nil for a random one
}

// IssueW4Consent issues a consent token on the HUMAN's side, after showing
// them the action: signed by human's key. cap needs CapAttest and must
// belong to human's context.
func IssueW4Consent(cap *Capability, human *W4Agent, r W4ConsentRequest) (W4ConsentToken, error) {
	var t W4ConsentToken
	if err := sameContext(cap, human.cap, "human key"); err != nil {
		return t, err
	}
	var np *C.uint8_t
	var nonce [16]byte
	if r.Nonce != nil {
		nonce = *r.Nonce
		np = ptr16(&nonce)
	}
	err := human.h.use(func(hp unsafe.Pointer) error {
		return cap.gated(func(cp *C.zxv_cap) C.int32_t {
			return C.zxv_w4_consent_issue(cp, (*C.zxv_w4_agent)(hp), ptr32(&r.AgentID), ptr32(&r.Action), C.uint32_t(r.Scope),
				C.uint64_t(r.MaxVFV), C.uint64_t(r.IssuedMs), C.uint64_t(r.ExpiresMs), np, t.ptr())
		})
	})
	return t, err
}

// W4ConsentLog is a single-use log of consumed and revoked consent tokens.
// Calls on one log are serialized. Close it when done.
type W4ConsentLog struct{ h *handle }

// NewW4ConsentLog creates a log of capacity entries (1..W4ConsentLogMax).
func NewW4ConsentLog(capacity uint32) (*W4ConsentLog, error) {
	var r *C.zxv_w4_consent_log
	if err := call(func() C.int32_t { return C.zxv_w4_consent_log_create(C.uint32_t(capacity), &r) }); err != nil {
		return nil, err
	}
	l := &W4ConsentLog{newHandle("web4 consent log", unsafe.Pointer(r), func(p unsafe.Pointer) {
		C.zxv_w4_consent_log_destroy((*C.zxv_w4_consent_log)(p))
	})}
	l.h.exclusive = true
	runtime.SetFinalizer(l, func(l *W4ConsentLog) { _ = l.h.close() })
	return l, nil
}

// Close frees the log. Closing twice is a no-op.
func (l *W4ConsentLog) Close() error { return l.h.close() }

func (l *W4ConsentLog) do(f func(p *C.zxv_w4_consent_log) C.int32_t) error {
	return l.h.use(func(p unsafe.Pointer) error {
		return call(func() C.int32_t { return f((*C.zxv_w4_consent_log)(p)) })
	})
}

// W4ConsentNeed is what an action needs consent for.
type W4ConsentNeed struct {
	AgentID [32]byte // the agent acting
	Action  [32]byte // W4ActionDigest of the request
	Need    uint32   // nonzero W4Consent* bits
	Amount  uint64   // VFV minor units (checked when Need has W4ConsentMoney)
	NowMs   uint64   // the time of the check
}

// Check checks that token (nil: none presented) lets n.AgentID do n.Action,
// signed by the human whose key is humanPublicKey, valid at n.NowMs; then
// records it as used. nil means consent was given and is now spent.
// ErrConsent: no token, a mismatch, expired, a failing signature, or a
// token already used or revoked (the reason is in the error's detail).
// ZXV_E_CAPACITY: the log is full even after pruning (fail closed).
func (l *W4ConsentLog) Check(token *W4ConsentToken, humanPublicKey []byte, n W4ConsentNeed) error {
	if err := need(humanPublicKey, W4PKBytes, "human public key"); err != nil {
		return err
	}
	var tp *C.uint8_t
	var t W4ConsentToken
	if token != nil {
		t = *token
		tp = t.ptr()
	}
	return l.do(func(p *C.zxv_w4_consent_log) C.int32_t {
		return C.zxv_w4_consent_check(p, tp, bytePtr(humanPublicKey), ptr32(&n.AgentID), ptr32(&n.Action),
			C.uint32_t(n.Need), C.uint64_t(n.Amount), C.uint64_t(n.NowMs))
	})
}

// Revoke revokes token before use: later checks of it fail with
// ErrConsent. It needs no capability (it only takes authority away).
// Revoking twice is fine.
func (l *W4ConsentLog) Revoke(token *W4ConsentToken, nowMs uint64) error {
	t := *token
	return l.do(func(p *C.zxv_w4_consent_log) C.int32_t { return C.zxv_w4_consent_revoke(p, t.ptr(), C.uint64_t(nowMs)) })
}

// Prune drops entries whose token expired at or before nowMs and returns
// how many remain.
func (l *W4ConsentLog) Prune(nowMs uint64) (uint32, error) {
	var n C.uint32_t
	err := l.do(func(p *C.zxv_w4_consent_log) C.int32_t { return C.zxv_w4_consent_log_prune(p, C.uint64_t(nowMs), &n) })
	return uint32(n), err
}

// W4Message is an envelope to seal.
type W4Message struct {
	Kind    W4MsgKind       // W4Msg*
	To      [32]byte        // the receiver's agent id
	ReqID   uint64          // request id
	Chunk   uint32          // chunk index
	Tool    string          // a valid tool name
	Flags   uint32          // 0 or W4FlagFinal
	Payload []byte          // at most W4MsgMaxPayload bytes
	Consent *W4ConsentToken // a token to attach, or nil
}

// Seal builds, signs and encodes an envelope from this agent with the next
// sequence number, using the agent's capability (needs CapCryptoSign).
// ZXV_E_TOO_LONG: payload over W4MsgMaxPayload.
func (a *W4Agent) Seal(m W4Message, nowMs uint64) ([]byte, error) {
	cn, err := cstring(m.Tool)
	if err != nil {
		return nil, err
	}
	defer freeC(cn)
	var tp *C.uint8_t
	var t W4ConsentToken
	if m.Consent != nil {
		t = *m.Consent
		tp = t.ptr()
	}
	var out []byte
	err = a.gatedSeq(func(cp *C.zxv_cap, p unsafe.Pointer) error {
		var e error
		out, e = readBytes(W4EnvMax, func(b *C.uint8_t, c C.size_t, l *C.size_t) C.int32_t {
			return C.zxv_w4_env_seal(cp, (*C.zxv_w4_agent)(p), C.int32_t(m.Kind), ptr32(&m.To), C.uint64_t(m.ReqID),
				C.uint32_t(m.Chunk), cn, C.uint32_t(m.Flags), bytePtr(m.Payload), C.size_t(len(m.Payload)), tp,
				C.uint64_t(nowMs), b, c, l)
		})
		return e
	})
	return out, err
}

// Reply seals a reply to a REQUEST envelope addressed to this agent: to and
// from swapped, the same request id and tool. kind: W4MsgResponse,
// W4MsgStream or W4MsgError. The request's signature is not checked here
// (use VerifyW4Envelope first).
func (a *W4Agent) Reply(request []byte, kind W4MsgKind, chunk, flags uint32, payload []byte, nowMs uint64) ([]byte, error) {
	var out []byte
	err := a.gatedSeq(func(cp *C.zxv_cap, p unsafe.Pointer) error {
		var e error
		out, e = readBytes(W4EnvMax, func(b *C.uint8_t, c C.size_t, l *C.size_t) C.int32_t {
			return C.zxv_w4_env_reply(cp, (*C.zxv_w4_agent)(p), bytePtr(request), C.size_t(len(request)), C.int32_t(kind),
				C.uint32_t(chunk), C.uint32_t(flags), bytePtr(payload), C.size_t(len(payload)), C.uint64_t(nowMs), b, c, l)
		})
		return e
	})
	return out, err
}

// W4Envelope is a decoded envelope's header.
type W4Envelope struct {
	Kind       W4MsgKind // REQUEST, RESPONSE, STREAM, CANCEL or ERROR
	Flags      uint32    // W4Flag* bits
	From       [32]byte  // the sender's agent id
	To         [32]byte  // the receiver's agent id
	Seq        uint64    // the sender's sequence number
	TsMs       uint64    // the sender's timestamp (Unix ms)
	ReqID      uint64    // request id
	Chunk      uint32    // chunk index
	Tool       string    // tool name
	PayloadLen uint32    // payload bytes
}

// DecodeW4Envelope parses an envelope without checking its signature.
// ZXV_E_FORMAT: malformed.
func DecodeW4Envelope(env []byte) (W4Envelope, error) {
	var e W4Envelope
	var kind C.int32_t
	var flags, chunk, pl C.uint32_t
	var seq, ts, req C.uint64_t
	var tool [32]C.char
	err := call(func() C.int32_t {
		return C.zxv_w4_env_decode(bytePtr(env), C.size_t(len(env)), &kind, &flags, ptr32(&e.From), ptr32(&e.To), &seq, &ts, &req,
			&chunk, &tool[0], &pl)
	})
	if err != nil {
		return W4Envelope{}, err
	}
	e.Kind, e.Flags, e.Seq, e.TsMs, e.ReqID = W4MsgKind(kind), uint32(flags), uint64(seq), uint64(ts), uint64(req)
	e.Chunk, e.Tool, e.PayloadLen = uint32(chunk), C.GoString(&tool[0]), uint32(pl)
	return e, nil
}

// W4EnvelopePayload returns an envelope's payload. ZXV_E_FORMAT: malformed.
func W4EnvelopePayload(env []byte) ([]byte, error) {
	return readBytes(W4MsgMaxPayload, func(b *C.uint8_t, c C.size_t, l *C.size_t) C.int32_t {
		return C.zxv_w4_env_payload(bytePtr(env), C.size_t(len(env)), b, c, l)
	})
}

// W4EnvelopeConsent returns the consent token attached to an envelope.
// ErrNotFound: none attached.
func W4EnvelopeConsent(env []byte) (W4ConsentToken, error) {
	var t W4ConsentToken
	err := call(func() C.int32_t { return C.zxv_w4_env_consent(bytePtr(env), C.size_t(len(env)), t.ptr()) })
	return t, err
}

// VerifyW4Envelope returns nil if senderPublicKey is the envelope's sender
// and its signature verifies, else ErrVerificationFailed. Replay, timestamp and rate
// checks are the receiver's job.
func VerifyW4Envelope(env, senderPublicKey []byte) error {
	if err := need(senderPublicKey, W4PKBytes, "sender public key"); err != nil {
		return err
	}
	return call(func() C.int32_t {
		return C.zxv_w4_env_verify(bytePtr(env), C.size_t(len(env)), bytePtr(senderPublicKey))
	})
}
