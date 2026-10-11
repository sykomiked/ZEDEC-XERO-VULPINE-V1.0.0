// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv

/*
#include <stdlib.h>
#include <string.h>
#include "zxv_api.h"
*/
import "C"

import (
	"crypto/subtle"
	"fmt"
	"runtime"
	"sync"
	"unsafe"
)

// Sizes of the FIPS 203 / FIPS 204 / RFC 8032 objects.
const (
	MLKEM1024EKBytes = 1568
	MLKEM1024DKBytes = 3168
	MLKEM1024CTBytes = 1568
	MLKEMSSBytes     = 32
	MLDSA87PKBytes   = 2592
	MLDSA87SKBytes   = 4896
	MLDSA87SigBytes  = 4627
	Ed25519PKBytes   = 32
	Ed25519SKBytes   = 32
	Ed25519SigBytes  = 64
)

// SHA3256 returns SHA3-256 of data.
func SHA3256(data []byte) [32]byte {
	var out [32]byte
	C.zxv_sha3_256(bytePtr(data), C.size_t(len(data)), (*C.uint8_t)(unsafe.Pointer(&out[0])))
	return out
}

// SHAKE256 returns outLen bytes of SHAKE256 of data.
func SHAKE256(data []byte, outLen int) []byte {
	out := make([]byte, outLen)
	C.zxv_shake256(bytePtr(data), C.size_t(len(data)), bytePtr(out), C.size_t(outLen))
	return out
}

// Keccak256 returns Ethereum's Keccak-256 of data (pre-FIPS padding).
func Keccak256(data []byte) [32]byte {
	var out [32]byte
	C.zxv_keccak256(bytePtr(data), C.size_t(len(data)), (*C.uint8_t)(unsafe.Pointer(&out[0])))
	return out
}

// Random fills b from the OS CSPRNG.
func Random(b []byte) error {
	return call(func() C.int32_t { return C.zxv_random(bytePtr(b), C.size_t(len(b))) })
}

// SecretKey holds secret bytes (a secret key or a shared secret) in memory
// allocated with C.malloc, outside the Go heap, so the garbage collector
// never copies it. Close wipes it with zxv_secure_zero and frees it; a
// finalizer does the same as a backstop. The bytes leave only through
// ExportBytesUnsafe.
type SecretKey struct {
	mu  sync.RWMutex
	ptr unsafe.Pointer
	n   int
}

func newSecret(n int) *SecretKey {
	p := C.malloc(C.size_t(n))
	if p == nil {
		panic("zxv: out of memory allocating a secret")
	}
	C.zxv_secure_zero(p, C.size_t(n))
	s := &SecretKey{ptr: p, n: n}
	runtime.SetFinalizer(s, func(s *SecretKey) { s.Close() })
	return s
}

// SecretKeyFromBytes copies b into off-heap memory (e.g. a key loaded from
// a vault). Wipe your own copy of b afterwards.
func SecretKeyFromBytes(b []byte) *SecretKey {
	s := newSecret(len(b))
	if len(b) > 0 {
		C.memcpy(s.ptr, unsafe.Pointer(&b[0]), C.size_t(len(b)))
	}
	return s
}

// Len returns the size in bytes.
func (s *SecretKey) Len() int { return s.n }

// Close wipes and frees the secret. It is safe to call more than once.
func (s *SecretKey) Close() {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.ptr != nil {
		C.zxv_secure_zero(s.ptr, C.size_t(s.n))
		C.free(s.ptr)
		s.ptr = nil
	}
}

// ExportBytesUnsafe returns a Go copy of the secret. It DEFEATS the
// wiping: the copy lives on the Go heap and is never zeroed.
func (s *SecretKey) ExportBytesUnsafe() ([]byte, error) {
	var out []byte
	err := s.use(s.n, "secret", func(p *C.uint8_t) error {
		out = C.GoBytes(unsafe.Pointer(p), C.int(s.n))
		return nil
	})
	return out, err
}

// Equal compares two secrets in constant time.
func (s *SecretKey) Equal(o *SecretKey) bool {
	a, err1 := s.ExportBytesUnsafe()
	b, err2 := o.ExportBytesUnsafe()
	defer wipe(a)
	defer wipe(b)
	return err1 == nil && err2 == nil && subtle.ConstantTimeCompare(a, b) == 1
}

// String never prints the secret.
func (s *SecretKey) String() string { return fmt.Sprintf("SecretKey(%d bytes, redacted)", s.n) }

func (s *SecretKey) use(n int, what string, f func(p *C.uint8_t) error) error {
	s.mu.RLock()
	defer s.mu.RUnlock()
	if s.ptr == nil {
		return &closedError{"secret key"}
	}
	if n >= 0 && s.n != n {
		return newError(StatusArg, fmt.Sprintf("%s must be %d bytes, got %d", what, n, s.n))
	}
	return f((*C.uint8_t)(s.ptr))
}

func wipe(b []byte) {
	if len(b) > 0 {
		C.zxv_secure_zero(unsafe.Pointer(&b[0]), C.size_t(len(b)))
	}
}

func need(b []byte, n int, what string) error {
	if len(b) != n {
		return newError(StatusArg, fmt.Sprintf("%s must be %d bytes, got %d", what, n, len(b)))
	}
	return nil
}

// MLKEM1024 is FIPS 203 ML-KEM-1024 through a capability with CapCryptoKEM.
// Not FIPS 140-3 validated.
type MLKEM1024 struct{ cap *Capability }

// NewMLKEM1024 returns ML-KEM-1024 through cap.
func NewMLKEM1024(cap *Capability) *MLKEM1024 { return &MLKEM1024{cap} }

// KeyGen returns a fresh (encapsulation key, decapsulation key).
func (k *MLKEM1024) KeyGen() ([]byte, *SecretKey, error) {
	ek := make([]byte, MLKEM1024EKBytes)
	dk := newSecret(MLKEM1024DKBytes)
	err := k.cap.gated(func(cp *C.zxv_cap) C.int32_t { return C.zxv_mlkem1024_keygen(cp, bytePtr(ek), (*C.uint8_t)(dk.ptr)) })
	if err != nil {
		dk.Close()
		return nil, nil, err
	}
	return ek, dk, nil
}

// KeyGenDerand derives a key pair from d and z (FIPS 203 KeyGen_internal),
// for known-answer tests.
func (k *MLKEM1024) KeyGenDerand(d, z [32]byte) ([]byte, *SecretKey, error) {
	ek := make([]byte, MLKEM1024EKBytes)
	dk := newSecret(MLKEM1024DKBytes)
	err := k.cap.gated(func(cp *C.zxv_cap) C.int32_t {
		return C.zxv_mlkem1024_keygen_derand(cp, (*C.uint8_t)(unsafe.Pointer(&d[0])), (*C.uint8_t)(unsafe.Pointer(&z[0])), bytePtr(ek), (*C.uint8_t)(dk.ptr))
	})
	if err != nil {
		dk.Close()
		return nil, nil, err
	}
	return ek, dk, nil
}

// Encapsulate returns (ciphertext, shared secret) for ek. A malformed key
// is ZXV_E_FORMAT.
func (k *MLKEM1024) Encapsulate(ek []byte) ([]byte, *SecretKey, error) {
	return k.encaps(ek, nil)
}

// EncapsulateDerand encapsulates with message m, for known-answer tests.
func (k *MLKEM1024) EncapsulateDerand(ek []byte, m [32]byte) ([]byte, *SecretKey, error) {
	return k.encaps(ek, m[:])
}

func (k *MLKEM1024) encaps(ek, m []byte) ([]byte, *SecretKey, error) {
	if err := need(ek, MLKEM1024EKBytes, "encapsulation key"); err != nil {
		return nil, nil, err
	}
	ct := make([]byte, MLKEM1024CTBytes)
	ss := newSecret(MLKEMSSBytes)
	err := k.cap.gated(func(cp *C.zxv_cap) C.int32_t {
		if m == nil {
			return C.zxv_mlkem1024_encaps(cp, bytePtr(ek), bytePtr(ct), (*C.uint8_t)(ss.ptr))
		}
		return C.zxv_mlkem1024_encaps_derand(cp, bytePtr(ek), bytePtr(m), bytePtr(ct), (*C.uint8_t)(ss.ptr))
	})
	if err != nil {
		ss.Close()
		return nil, nil, err
	}
	return ct, ss, nil
}

// Decapsulate returns the shared secret of ct under dk.
func (k *MLKEM1024) Decapsulate(dk *SecretKey, ct []byte) (*SecretKey, error) {
	if err := need(ct, MLKEM1024CTBytes, "ciphertext"); err != nil {
		return nil, err
	}
	ss := newSecret(MLKEMSSBytes)
	err := dk.use(MLKEM1024DKBytes, "decapsulation key", func(d *C.uint8_t) error {
		return k.cap.gated(func(cp *C.zxv_cap) C.int32_t { return C.zxv_mlkem1024_decaps(cp, d, bytePtr(ct), (*C.uint8_t)(ss.ptr)) })
	})
	if err != nil {
		ss.Close()
		return nil, err
	}
	return ss, nil
}

// MLDSA87 is FIPS 204 ML-DSA-87 through a capability with CapCryptoSign.
// context is the FIPS 204 context string (<= 255 bytes, may be nil). Not
// FIPS 140-3 validated.
type MLDSA87 struct{ cap *Capability }

// NewMLDSA87 returns ML-DSA-87 through cap.
func NewMLDSA87(cap *Capability) *MLDSA87 { return &MLDSA87{cap} }

// KeyGen returns a fresh (public key, secret key).
func (d *MLDSA87) KeyGen() ([]byte, *SecretKey, error) {
	return d.keygen(nil)
}

// KeyGenFromSeed derives a key pair from a 32-byte seed.
func (d *MLDSA87) KeyGenFromSeed(seed [32]byte) ([]byte, *SecretKey, error) {
	return d.keygen(seed[:])
}

func (d *MLDSA87) keygen(seed []byte) ([]byte, *SecretKey, error) {
	pk := make([]byte, MLDSA87PKBytes)
	sk := newSecret(MLDSA87SKBytes)
	err := d.cap.gated(func(cp *C.zxv_cap) C.int32_t {
		if seed == nil {
			return C.zxv_mldsa87_keygen(cp, bytePtr(pk), (*C.uint8_t)(sk.ptr))
		}
		return C.zxv_mldsa87_keygen_seed(cp, bytePtr(seed), bytePtr(pk), (*C.uint8_t)(sk.ptr))
	})
	if err != nil {
		sk.Close()
		return nil, nil, err
	}
	return pk, sk, nil
}

// Sign returns a hedged signature (fresh randomness).
func (d *MLDSA87) Sign(sk *SecretKey, msg, context []byte) ([]byte, error) {
	return d.sign(sk, msg, context, false)
}

// SignDeterministic returns a deterministic signature.
func (d *MLDSA87) SignDeterministic(sk *SecretKey, msg, context []byte) ([]byte, error) {
	return d.sign(sk, msg, context, true)
}

func (d *MLDSA87) sign(sk *SecretKey, msg, context []byte, det bool) ([]byte, error) {
	sig := make([]byte, MLDSA87SigBytes)
	err := sk.use(MLDSA87SKBytes, "secret key", func(s *C.uint8_t) error {
		return d.cap.gated(func(cp *C.zxv_cap) C.int32_t {
			if det {
				return C.zxv_mldsa87_sign_deterministic(cp, s, bytePtr(msg), C.size_t(len(msg)), bytePtr(context), C.size_t(len(context)), bytePtr(sig))
			}
			return C.zxv_mldsa87_sign(cp, s, bytePtr(msg), C.size_t(len(msg)), bytePtr(context), C.size_t(len(context)), bytePtr(sig))
		})
	})
	if err != nil {
		return nil, err
	}
	return sig, nil
}

// Verify returns nil if sig is valid, else an error matching
// ErrVerificationFailed.
func (d *MLDSA87) Verify(pk, msg, context, sig []byte) error {
	if err := need(pk, MLDSA87PKBytes, "public key"); err != nil {
		return err
	}
	if err := need(sig, MLDSA87SigBytes, "signature"); err != nil {
		return err
	}
	return d.cap.gated(func(cp *C.zxv_cap) C.int32_t {
		return C.zxv_mldsa87_verify(cp, bytePtr(pk), bytePtr(msg), C.size_t(len(msg)), bytePtr(context), C.size_t(len(context)), bytePtr(sig))
	})
}

// Ed25519 is RFC 8032 Ed25519 through a capability with CapCryptoSign.
type Ed25519 struct{ cap *Capability }

// NewEd25519 returns Ed25519 through cap.
func NewEd25519(cap *Capability) *Ed25519 { return &Ed25519{cap} }

// KeyGen returns a fresh (public key, secret seed).
func (e *Ed25519) KeyGen() ([32]byte, *SecretKey, error) {
	var pk [32]byte
	sk := newSecret(Ed25519SKBytes)
	err := e.cap.gated(func(cp *C.zxv_cap) C.int32_t {
		return C.zxv_ed25519_keygen(cp, (*C.uint8_t)(unsafe.Pointer(&pk[0])), (*C.uint8_t)(sk.ptr))
	})
	if err != nil {
		sk.Close()
		return pk, nil, err
	}
	return pk, sk, nil
}

// PublicKey returns the public key of a secret seed.
func (e *Ed25519) PublicKey(sk *SecretKey) ([32]byte, error) {
	var pk [32]byte
	err := sk.use(Ed25519SKBytes, "Ed25519 secret key", func(s *C.uint8_t) error {
		return e.cap.gated(func(cp *C.zxv_cap) C.int32_t {
			return C.zxv_ed25519_public(cp, s, (*C.uint8_t)(unsafe.Pointer(&pk[0])))
		})
	})
	return pk, err
}

// Sign signs msg.
func (e *Ed25519) Sign(sk *SecretKey, msg []byte) ([64]byte, error) {
	var sig [64]byte
	err := sk.use(Ed25519SKBytes, "Ed25519 secret key", func(s *C.uint8_t) error {
		return e.cap.gated(func(cp *C.zxv_cap) C.int32_t {
			return C.zxv_ed25519_sign(cp, s, bytePtr(msg), C.size_t(len(msg)), (*C.uint8_t)(unsafe.Pointer(&sig[0])))
		})
	})
	return sig, err
}

// Verify returns nil if valid, else an error matching ErrVerificationFailed.
func (e *Ed25519) Verify(pk [32]byte, msg []byte, sig [64]byte) error {
	return e.cap.gated(func(cp *C.zxv_cap) C.int32_t {
		return C.zxv_ed25519_verify(cp, (*C.uint8_t)(unsafe.Pointer(&pk[0])), bytePtr(msg), C.size_t(len(msg)), (*C.uint8_t)(unsafe.Pointer(&sig[0])))
	})
}
