// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv

// Hardware emulation and hardware updates (ABI 1.4).
//
//   - HwProfile: a ZHP-1 machine profile, the "hardware ROM" (plain data,
//     no capability): probe this host, take the public reference machine
//     R1, present R1 on a host, encode, digest and match.
//   - HwMatrix: functions (engine contracts) served by interchangeable,
//     proven implementations. Every matrix starts with the standard
//     engines (HwFnSHA256 ... HwFnTensor), each served by a proven SOFT
//     implementation. Needs CapHWMatrix; contract packages need
//     CapHWReconf.
//   - HwRelease: release keys and signed packages (ML-DSA-87 AND
//     SLH-DSA-SHAKE-256s). Needs CapCryptoSign. SLH-DSA signing takes
//     seconds: building is a release step.
//   - HwRegion: a reconfigurable region updated with signed images: stage,
//     trial, commit or roll back, every step an event and a record in a
//     hash-chained audit log. Needs CapHWReconf.
//
// Everything is integers.

/*
#include <stdlib.h>
#include "zxv_api.h"
#include "shim.h"
*/
import "C"

import (
	"errors"
	"fmt"
	"runtime"
	"runtime/cgo"
	"sync"
	"unsafe"
)

// The standard engines every matrix starts with.
const (
	HwFnSHA256  uint32 = C.ZXV_HW_FN_SHA256  // op 0: SHA-256
	HwFnKeccak  uint32 = C.ZXV_HW_FN_KECCAK  // op 0/1 SHA3-256/512, op 2/3 SHAKE
	HwFnMAC     uint32 = C.ZXV_HW_FN_MAC     // op 0: HMAC-SHA256 (key-bearing)
	HwFnGCD     uint32 = C.ZXV_HW_FN_GCD     // op 0: n pairs of u64 -> n gcds
	HwFnLogic64 uint32 = C.ZXV_HW_FN_LOGIC64 // 64-lane four-valued AND / OR / NOT
	HwFnTensor  uint32 = C.ZXV_HW_FN_TENSOR  // op 0: one Q8_0 row dot
)

// Sizes and fixed ids.
const (
	HwResHost        uint32 = C.ZXV_HW_RES_HOST // the resource every matrix starts with: this CPU
	HwRoot           uint32 = C.ZXV_HW_ROOT     // parent id of a root profile node
	HwExtMax                = int(C.ZXV_HW_EXT_MAX)
	HwPKBytes               = int(C.ZXV_HW_PK_BYTES) // ML-DSA-87 key || SLH-DSA key
	HwMLDSASKBytes          = int(C.ZXV_HW_MLDSA_SK_BYTES)
	HwSLHDSASKBytes         = int(C.ZXV_HW_SLHDSA_SK_BYTES)
	HwSigBytes              = int(C.ZXV_HW_SIG_BYTES)
	HwHeaderBytes           = int(C.ZXV_HW_HEADER_BYTES)
	HwCallOutDefault        = 512 // output capacity of HwMatrix.Call (largest golden-vector output)
)

// Component classes (ZXV_HW_CLASS_*). Classes from HwClassPrivate up are an
// implementer's own.
const (
	HwClassMachine       uint32 = C.ZXV_HW_CLASS_MACHINE
	HwClassCPUCluster    uint32 = C.ZXV_HW_CLASS_CPU_CLUSTER
	HwClassCPUCore       uint32 = C.ZXV_HW_CLASS_CPU_CORE
	HwClassCache         uint32 = C.ZXV_HW_CLASS_CACHE
	HwClassMemory        uint32 = C.ZXV_HW_CLASS_MEMORY
	HwClassInterconnect  uint32 = C.ZXV_HW_CLASS_INTERCONNECT
	HwClassMatrixUnit    uint32 = C.ZXV_HW_CLASS_MATRIX_UNIT
	HwClassVectorUnit    uint32 = C.ZXV_HW_CLASS_VECTOR_UNIT
	HwClassTernaryUnit   uint32 = C.ZXV_HW_CLASS_TERNARY_UNIT
	HwClassHashUnit      uint32 = C.ZXV_HW_CLASS_HASH_UNIT
	HwClassCryptoUnit    uint32 = C.ZXV_HW_CLASS_CRYPTO_UNIT
	HwClassCodecUnit     uint32 = C.ZXV_HW_CLASS_CODEC_UNIT
	HwClassReconfFabric  uint32 = C.ZXV_HW_CLASS_RECONF_FABRIC
	HwClassStorage       uint32 = C.ZXV_HW_CLASS_STORAGE
	HwClassNetwork       uint32 = C.ZXV_HW_CLASS_NETWORK
	HwClassRadio         uint32 = C.ZXV_HW_CLASS_RADIO
	HwClassSensor        uint32 = C.ZXV_HW_CLASS_SENSOR
	HwClassActuator      uint32 = C.ZXV_HW_CLASS_ACTUATOR
	HwClassClock         uint32 = C.ZXV_HW_CLASS_CLOCK
	HwClassPower         uint32 = C.ZXV_HW_CLASS_POWER
	HwClassSecureElement uint32 = C.ZXV_HW_CLASS_SECURE_ELEMENT
	HwClassDisplay       uint32 = C.ZXV_HW_CLASS_DISPLAY
	HwClassAudio         uint32 = C.ZXV_HW_CLASS_AUDIO
	HwClassWire          uint32 = C.ZXV_HW_CLASS_WIRE
	HwClassGPU           uint32 = C.ZXV_HW_CLASS_GPU
	HwClassPackage       uint32 = C.ZXV_HW_CLASS_PACKAGE
	HwClassLane          uint32 = C.ZXV_HW_CLASS_LANE
	HwClassLogicUnit     uint32 = C.ZXV_HW_CLASS_LOGIC_UNIT
	HwClassNTTUnit       uint32 = C.ZXV_HW_CLASS_NTT_UNIT
	HwClassFrameUnit     uint32 = C.ZXV_HW_CLASS_FRAME_UNIT
	HwClassGCDUnit       uint32 = C.ZXV_HW_CLASS_GCD_UNIT
	HwClassMemChannel    uint32 = C.ZXV_HW_CLASS_MEM_CHANNEL
	HwClassSecureIsland  uint32 = C.ZXV_HW_CLASS_SECURE_ISLAND
	HwClassIO            uint32 = C.ZXV_HW_CLASS_IO
	HwClassPrivate       uint32 = C.ZXV_HW_CLASS_PRIVATE
)

// Capability keys of a profile node (ZXV_HW_CAP_*): bit 15 critical, bits
// 14-13 comparison (MIN, EXACT, MASK, INFO), bits 12-0 id.
const (
	HwCapWordBits      uint32 = C.ZXV_HW_CAP_WORD_BITS
	HwCapCount         uint32 = C.ZXV_HW_CAP_COUNT
	HwCapFreqKHz       uint32 = C.ZXV_HW_CAP_FREQ_KHZ
	HwCapBytes         uint32 = C.ZXV_HW_CAP_BYTES
	HwCapLineBytes     uint32 = C.ZXV_HW_CAP_LINE_BYTES
	HwCapISA           uint32 = C.ZXV_HW_CAP_ISA
	HwCapIntWidths     uint32 = C.ZXV_HW_CAP_INT_WIDTHS
	HwCapRadix         uint32 = C.ZXV_HW_CAP_RADIX
	HwCapMACsPerCycle  uint32 = C.ZXV_HW_CAP_MACS_PER_CYCLE
	HwCapSIMD          uint32 = C.ZXV_HW_CAP_SIMD
	HwCapTickNs        uint32 = C.ZXV_HW_CAP_TICK_NS
	HwCapRegions       uint32 = C.ZXV_HW_CAP_REGIONS
	HwCapBandwidthMBps uint32 = C.ZXV_HW_CAP_BANDWIDTH_MBPS
	HwCapLatencyNs     uint32 = C.ZXV_HW_CAP_LATENCY_NS
	HwCapPQAlgs        uint32 = C.ZXV_HW_CAP_PQ_ALGS
	HwCapHashAlgs      uint32 = C.ZXV_HW_CAP_HASH_ALGS
	HwCapWireBits      uint32 = C.ZXV_HW_CAP_WIRE_BITS
	HwCapPeakOpsMHz    uint32 = C.ZXV_HW_CAP_PEAK_OPS_MHZ
	HwCapContractID    uint32 = C.ZXV_HW_CAP_CONTRACT_ID  // the engine contract (HwFn*)
	HwCapContractVer   uint32 = C.ZXV_HW_CAP_CONTRACT_VER // its golden-vector version
	HwCapKind          uint32 = C.ZXV_HW_CAP_KIND         // the HwKind serving the node
	HwCapCapacityClass uint32 = C.ZXV_HW_CAP_CAPACITY_CLASS
	HwCapPerfBucket    uint32 = C.ZXV_HW_CAP_PERF_BUCKET
	HwCapPowerBucket   uint32 = C.ZXV_HW_CAP_POWER_BUCKET // HwPower scale

	HwISAX8664   uint64 = C.ZXV_HW_ISA_X86_64
	HwISAARM64   uint64 = C.ZXV_HW_ISA_ARM64
	HwISARISCV64 uint64 = C.ZXV_HW_ISA_RISCV64
	HwISAWASM32  uint64 = C.ZXV_HW_ISA_WASM32
)

// HwKind is how an implementation is built, in the order the matrix
// prefers them at equal cost.
type HwKind int32

// Implementation kinds.
const (
	HwKindHard   HwKind = C.ZXV_HW_KIND_HARD   // silicon built for the function
	HwKindFabric HwKind = C.ZXV_HW_KIND_FABRIC // a reconfigurable fabric region
	HwKindMicro  HwKind = C.ZXV_HW_KIND_MICRO  // a microcoded unit or soft core
	HwKindPeer   HwKind = C.ZXV_HW_KIND_PEER   // another local component standing in
	HwKindSoft   HwKind = C.ZXV_HW_KIND_SOFT   // a software emulator
	HwKindRemote HwKind = C.ZXV_HW_KIND_REMOTE // another machine (never key-bearing work)
)

// String returns the kind name.
func (k HwKind) String() string {
	names := []string{"HARD", "FABRIC", "MICRO", "PEER", "SOFT", "REMOTE"}
	if k >= 0 && int(k) < len(names) {
		return names[k]
	}
	return fmt.Sprintf("HwKind(%d)", int32(k))
}

// HwResourceState is the state of a matrix resource.
type HwResourceState int32

// Resource states.
const (
	HwResOK            HwResourceState = C.ZXV_HW_RES_OK
	HwResFaulted       HwResourceState = C.ZXV_HW_RES_FAULTED
	HwResReconfiguring HwResourceState = C.ZXV_HW_RES_RECONFIGURING
	HwResOffline       HwResourceState = C.ZXV_HW_RES_OFFLINE
)

// HwPolicy is a set of placement policy flags.
type HwPolicy uint32

// Policy flags.
const (
	HwPolicyKeyBearing HwPolicy = C.ZXV_HW_POLICY_KEY_BEARING // always local, never memoized; sticky
	HwPolicyLocalOnly  HwPolicy = C.ZXV_HW_POLICY_LOCAL_ONLY  // no REMOTE implementation
)

// Has reports whether every bit of f is set.
func (p HwPolicy) Has(f HwPolicy) bool { return p&f == f }

// HwProviderFlags is the status of an implementation.
type HwProviderFlags uint32

// Implementation status flags.
const (
	HwPFFault    HwProviderFlags = C.ZXV_HW_PF_FAULT    // its resource is faulted or offline
	HwPFDisputed HwProviderFlags = C.ZXV_HW_PF_DISPUTED // a shadow check found it disagreeing
	HwPFFailed   HwProviderFlags = C.ZXV_HW_PF_FAILED   // it failed the golden vectors
	HwPFUnproven HwProviderFlags = C.ZXV_HW_PF_UNPROVEN // not proven against the current contract
	HwPFDisabled HwProviderFlags = C.ZXV_HW_PF_DISABLED
	HwPFPolicy   HwProviderFlags = C.ZXV_HW_PF_POLICY  // the function's policy excludes it
	HwPFServing  HwProviderFlags = C.ZXV_HW_PF_SERVING // serving the function now
)

// Has reports whether every bit of f is set.
func (p HwProviderFlags) Has(f HwProviderFlags) bool { return p&f == f }

// HwPower is a coarse power class.
type HwPower uint32

// Power classes.
const (
	HwPowerUnknown HwPower = C.ZXV_HW_POWER_UNKNOWN
	HwPower10mW    HwPower = C.ZXV_HW_POWER_10MW
	HwPower100mW   HwPower = C.ZXV_HW_POWER_100MW
	HwPower1W      HwPower = C.ZXV_HW_POWER_1W
	HwPower10W     HwPower = C.ZXV_HW_POWER_10W
	HwPower100W    HwPower = C.ZXV_HW_POWER_100W
	HwPowerMore    HwPower = C.ZXV_HW_POWER_MORE
)

// HwImageKind is the kind of a package.
type HwImageKind uint32

// Package kinds.
const (
	HwImgBitstream HwImageKind = C.ZXV_HW_IMG_BITSTREAM
	HwImgPartial   HwImageKind = C.ZXV_HW_IMG_PARTIAL
	HwImgMicrocode HwImageKind = C.ZXV_HW_IMG_MICROCODE
	HwImgFirmware  HwImageKind = C.ZXV_HW_IMG_FIRMWARE
	HwImgVectors   HwImageKind = C.ZXV_HW_IMG_VECTORS // a new golden-vector contract
)

// HwRegionState is the state of a region.
type HwRegionState int32

// Region states.
const (
	HwRegionEmpty     HwRegionState = C.ZXV_HW_REGION_EMPTY
	HwRegionActive    HwRegionState = C.ZXV_HW_REGION_ACTIVE
	HwRegionStaged    HwRegionState = C.ZXV_HW_REGION_STAGED
	HwRegionProbation HwRegionState = C.ZXV_HW_REGION_PROBATION
	HwRegionHalted    HwRegionState = C.ZXV_HW_REGION_HALTED
)

// HwEventKind is a region event or audit record kind.
type HwEventKind int32

// Events.
const (
	HwEvStaged      HwEventKind = C.ZXV_HW_EV_STAGED      // verified and loaded into the inactive slot
	HwEvTested      HwEventKind = C.ZXV_HW_EV_TESTED      // switched to and proven: probation begins
	HwEvActivated   HwEventKind = C.ZXV_HW_EV_ACTIVATED   // committed: permanent, floor raised
	HwEvRolledBack  HwEventKind = C.ZXV_HW_EV_ROLLED_BACK // back to the previous image (or S0)
	HwEvRejected    HwEventKind = C.ZXV_HW_EV_REJECTED    // refused before loading
	HwEvQuarantined HwEventKind = C.ZXV_HW_EV_QUARANTINED // failed in hardware; payload blocked
	HwEvContract    HwEventKind = C.ZXV_HW_EV_CONTRACT    // a function's golden vectors replaced
)

// String returns the event name.
func (e HwEventKind) String() string {
	names := []string{"", "STAGED", "TESTED", "ACTIVATED", "ROLLED_BACK", "REJECTED", "QUARANTINED", "CONTRACT"}
	if e > 0 && int(e) < len(names) {
		return names[e]
	}
	return fmt.Sprintf("HwEventKind(%d)", int32(e))
}

// statusFromError maps a callback's error to a negative status.
func statusFromError(err error) C.int32_t {
	var ze *Error
	if errors.As(err, &ze) && ze.Status < 0 {
		return C.int32_t(ze.Status)
	}
	return C.ZXV_E_INTERNAL
}

func ptr32(b *[32]byte) *C.uint8_t { return (*C.uint8_t)(unsafe.Pointer(&b[0])) }

// ===== Profiles ============================================================

// HwNode is one node of a profile.
type HwNode struct {
	ID, Parent, Class uint32
	Caps              uint32 // how many capabilities it carries
}

// HwMatch is how many target components a host runs natively and how many
// it emulates.
type HwMatch struct{ Native, Emulated uint32 }

// HwPairKind is how one target component is served on a host.
type HwPairKind int32

// Pairings.
const (
	HwNative  HwPairKind = C.ZXV_HW_NATIVE  // a host component satisfies it (HostID)
	HwEmulate HwPairKind = C.ZXV_HW_EMULATE // nothing does: emulated (UnmetKey, 0 if no such class)
	HwGroup   HwPairKind = C.ZXV_HW_GROUP   // a device or package node: nothing to run
)

// HwPair is the pairing of one target node.
type HwPair struct {
	NeedID   uint32
	Kind     HwPairKind
	HostID   uint32
	UnmetKey uint32
}

// HwProfile is a ZHP-1 machine profile: a canonical tree of abstract
// components with integer capability descriptors and an opaque private
// extension per node. Plain data: no capability is needed. Safe for
// concurrent use.
type HwProfile struct{ h *handle }

func wrapProfile(f func(out **C.zxv_hw_profile) C.int32_t) (*HwProfile, error) {
	var r *C.zxv_hw_profile
	if err := call(func() C.int32_t { return f(&r) }); err != nil {
		return nil, err
	}
	p := &HwProfile{}
	p.h = newHandle("hardware profile", unsafe.Pointer(r), func(q unsafe.Pointer) { C.zxv_hw_profile_destroy((*C.zxv_hw_profile)(q)) })
	p.h.exclusive = true
	runtime.SetFinalizer(p, func(p *HwProfile) { _ = p.h.close() })
	return p, nil
}

// NewHwProfile returns an empty profile.
func NewHwProfile() (*HwProfile, error) {
	return wrapProfile(func(o **C.zxv_hw_profile) C.int32_t { return C.zxv_hw_profile_create(o) })
}

// HwProbeHost describes this machine: CPU cluster (ISA, cores, SIMD),
// memory, cache line. Only what the OS reports reliably.
func HwProbeHost() (*HwProfile, error) {
	return wrapProfile(func(o **C.zxv_hw_profile) C.int32_t { return C.zxv_hw_profile_probe_host(o) })
}

// HwReference returns a copy of the public VULPINE-class reference machine R1.
func HwReference() (*HwProfile, error) {
	return wrapProfile(func(o **C.zxv_hw_profile) C.int32_t { return C.zxv_hw_profile_reference(o) })
}

// HwPresent returns target as served on host: every node that runs
// something carries HwCapKind (HARD where the host has it, SOFT where it is
// emulated).
func HwPresent(host, target *HwProfile) (*HwProfile, error) {
	var out *HwProfile
	err := host.use(func(hp *C.zxv_hw_profile) error {
		return target.use(func(tp *C.zxv_hw_profile) error {
			var e error
			out, e = wrapProfile(func(o **C.zxv_hw_profile) C.int32_t { return C.zxv_hw_profile_present(hp, tp, o) })
			return e
		})
	})
	return out, err
}

// DecodeHwProfile parses a canonical ZHP-1 encoding (anything else:
// ErrUsage, ZXV_E_FORMAT).
func DecodeHwProfile(b []byte) (*HwProfile, error) {
	return wrapProfile(func(o **C.zxv_hw_profile) C.int32_t {
		return C.zxv_hw_profile_decode(bytePtr(b), C.size_t(len(b)), o)
	})
}

// Close destroys the profile.
func (p *HwProfile) Close() error { return p.h.close() }

func (p *HwProfile) use(f func(r *C.zxv_hw_profile) error) error {
	return p.h.use(func(q unsafe.Pointer) error { return f((*C.zxv_hw_profile)(q)) })
}

func (p *HwProfile) do(f func(r *C.zxv_hw_profile) C.int32_t) error {
	return p.use(func(r *C.zxv_hw_profile) error { return call(func() C.int32_t { return f(r) }) })
}

// Encode returns the canonical encoding.
func (p *HwProfile) Encode() ([]byte, error) {
	var out []byte
	err := p.use(func(r *C.zxv_hw_profile) error {
		runtime.LockOSThread()
		defer runtime.UnlockOSThread()
		var n C.size_t
		rc := C.zxv_hw_profile_encode(r, nil, 0, &n)
		if rc != C.ZXV_E_BUFFER {
			if rc != C.ZXV_OK {
				return newError(int32(rc), lastError())
			}
			out = []byte{}
			return nil
		}
		buf := make([]byte, int(n))
		if rc = C.zxv_hw_profile_encode(r, bytePtr(buf), n, &n); rc != C.ZXV_OK {
			return newError(int32(rc), lastError())
		}
		out = buf[:n]
		return nil
	})
	return out, err
}

// Digest returns SHA-256 of the canonical encoding: the profile's identity.
func (p *HwProfile) Digest() ([32]byte, error) {
	var d [32]byte
	err := p.do(func(r *C.zxv_hw_profile) C.int32_t { return C.zxv_hw_profile_digest(r, ptr32(&d)) })
	return d, err
}

// AddNode adds a node: ids strictly increase, parent first (or HwRoot).
func (p *HwProfile) AddNode(id, parent, class uint32) error {
	return p.do(func(r *C.zxv_hw_profile) C.int32_t {
		return C.zxv_hw_profile_node_add(r, C.uint32_t(id), C.uint32_t(parent), C.uint32_t(class))
	})
}

// SetCap sets a capability of a node.
func (p *HwProfile) SetCap(node, key uint32, value uint64) error {
	return p.do(func(r *C.zxv_hw_profile) C.int32_t {
		return C.zxv_hw_profile_cap_set(r, C.uint32_t(node), C.uint32_t(key), C.uint64_t(value))
	})
}

// Cap returns a capability of a node (ErrNotFound if it lacks it).
func (p *HwProfile) Cap(node, key uint32) (uint64, error) {
	var v C.uint64_t
	err := p.do(func(r *C.zxv_hw_profile) C.int32_t {
		return C.zxv_hw_profile_cap_get(r, C.uint32_t(node), C.uint32_t(key), &v)
	})
	return uint64(v), err
}

// SetExt sets a node's private extension: at most HwExtMax opaque bytes
// that ZXV carries, encodes and digests but never interprets.
func (p *HwProfile) SetExt(node uint32, ext []byte) error {
	return p.do(func(r *C.zxv_hw_profile) C.int32_t {
		return C.zxv_hw_profile_ext_set(r, C.uint32_t(node), bytePtr(ext), C.size_t(len(ext)))
	})
}

// Ext returns a node's private extension.
func (p *HwProfile) Ext(node uint32) ([]byte, error) {
	buf := make([]byte, HwExtMax)
	var n C.size_t
	err := p.do(func(r *C.zxv_hw_profile) C.int32_t {
		return C.zxv_hw_profile_ext_get(r, C.uint32_t(node), bytePtr(buf), C.size_t(len(buf)), &n)
	})
	if err != nil {
		return nil, err
	}
	return buf[:n], nil
}

// NodeCount returns the number of nodes.
func (p *HwProfile) NodeCount() (uint32, error) {
	var n C.uint32_t
	err := p.do(func(r *C.zxv_hw_profile) C.int32_t { return C.zxv_hw_profile_count(r, &n) })
	return uint32(n), err
}

// Node returns node index (0 .. NodeCount-1).
func (p *HwProfile) Node(index uint32) (HwNode, error) {
	var id, parent, cls, ncaps C.uint32_t
	err := p.do(func(r *C.zxv_hw_profile) C.int32_t {
		return C.zxv_hw_profile_node(r, C.uint32_t(index), &id, &parent, &cls, &ncaps)
	})
	return HwNode{ID: uint32(id), Parent: uint32(parent), Class: uint32(cls), Caps: uint32(ncaps)}, err
}

// Nodes returns every node, in order.
func (p *HwProfile) Nodes() ([]HwNode, error) {
	n, err := p.NodeCount()
	if err != nil {
		return nil, err
	}
	out := make([]HwNode, 0, n)
	for i := uint32(0); i < n; i++ {
		nd, err := p.Node(i)
		if err != nil {
			return nil, err
		}
		out = append(out, nd)
	}
	return out, nil
}

// HwMatchProfiles matches a target against a host.
func HwMatchProfiles(host, target *HwProfile) (HwMatch, error) {
	var nat, emu C.uint32_t
	err := host.use(func(hp *C.zxv_hw_profile) error {
		return target.do(func(tp *C.zxv_hw_profile) C.int32_t { return C.zxv_hw_profile_match(hp, tp, &nat, &emu) })
	})
	return HwMatch{Native: uint32(nat), Emulated: uint32(emu)}, err
}

// HwPairNode returns the pairing of target node index (in target order) on host.
func HwPairNode(host, target *HwProfile, index uint32) (HwPair, error) {
	var need, have, unmet C.uint32_t
	var kind C.int32_t
	err := host.use(func(hp *C.zxv_hw_profile) error {
		return target.do(func(tp *C.zxv_hw_profile) C.int32_t {
			return C.zxv_hw_profile_pair(hp, tp, C.uint32_t(index), &need, &kind, &have, &unmet)
		})
	})
	if err != nil {
		return HwPair{}, err
	}
	pr := HwPair{NeedID: uint32(need), Kind: HwPairKind(kind)}
	switch pr.Kind {
	case HwNative:
		pr.HostID = uint32(have)
	case HwEmulate:
		pr.UnmetKey = uint32(unmet)
	}
	return pr, nil
}

// ===== The matrix ==========================================================

// HwProviderFunc is an implementation: write at most len(out) bytes of
// output for (op, in) and return how many. A non-nil error is reported as
// its status (errors without a status, and panics, become
// ZXV_E_INTERNAL). in and out are valid only during the call. It runs on
// the calling goroutine with the matrix locked; it may call the matrix
// again for the functions it declared with AddDependency.
type HwProviderFunc func(op uint32, in, out []byte) (int, error)

// HwFunctionInfo is what HwMatrix.FunctionInfo reports.
type HwFunctionInfo struct {
	Contract uint32 // current contract version
	Policy   HwPolicy
	Vectors  uint32 // golden vectors of the current contract
}

// HwProviderInfo is what HwMatrix.ProviderInfo reports.
type HwProviderInfo struct {
	FnID            uint32
	Kind            HwKind
	Proof           Truth // TRUE serves, FALSE a vector failed, NEITHER no vectors
	Contract        uint32
	Flags           HwProviderFlags
	ThroughputClass uint32 // 0 (slowest) .. 15
	Power           HwPower
	Calls           uint64
	CostSpent       uint64
}

// HwLog is the head of an audit log.
type HwLog struct {
	Head  [32]byte
	Count uint32
}

// HwLogEntry is one audit record (regions and matrices).
type HwLogEntry struct {
	Event   HwEventKind
	Reason  uint32 // failure code, 0 for none
	ID      uint32 // region id (function id for HwEvContract)
	Version uint32
	Tag     Hexagram // forbidden for rejections and quarantines
	Subject [32]byte // SHA-256 of the package header
	Hash    [32]byte // the record's chain hash
}

type hwProvider struct{ f HwProviderFunc }

// HwMatrix is the emulation matrix: functions served by interchangeable,
// proven implementations (needs CapHWMatrix). A new matrix holds the
// HwResHost resource and the standard engines. Safe for concurrent use.
type HwMatrix struct {
	h    *handle
	cap  *Capability
	mu   sync.Mutex
	cgoh []cgo.Handle
}

// NewHwMatrix creates a matrix with the standard engines in cap's context.
func NewHwMatrix(cap *Capability) (*HwMatrix, error) {
	var r *C.zxv_hw_matrix
	if err := cap.gated(func(p *C.zxv_cap) C.int32_t { return C.zxv_hw_matrix_create_cap(p, &r) }); err != nil {
		return nil, err
	}
	m := &HwMatrix{cap: cap}
	m.h = newHandle("hardware matrix", unsafe.Pointer(r), func(p unsafe.Pointer) {
		C.zxv_hw_matrix_destroy((*C.zxv_hw_matrix)(p))
		m.mu.Lock()
		for _, h := range m.cgoh {
			h.Delete()
		}
		m.cgoh = nil
		m.mu.Unlock()
	}, cap.h)
	runtime.SetFinalizer(m, func(m *HwMatrix) { _ = m.h.close() })
	return m, nil
}

// Close destroys the matrix. It fails while a region made on it is open.
func (m *HwMatrix) Close() error { return m.h.close() }

func (m *HwMatrix) gated(f func(cp *C.zxv_cap, mx *C.zxv_hw_matrix) C.int32_t) error {
	return m.h.use(func(r unsafe.Pointer) error {
		return m.cap.gated(func(cp *C.zxv_cap) C.int32_t { return f(cp, (*C.zxv_hw_matrix)(r)) })
	})
}

// AddFunction adds a function (name: UTF-8, at most 23 bytes; "" for none).
func (m *HwMatrix) AddFunction(fnID uint32, name string) error {
	var n *C.char
	if name != "" {
		var err error
		if n, err = cstring(name); err != nil {
			return err
		}
		defer freeC(n)
	}
	return m.gated(func(cp *C.zxv_cap, mx *C.zxv_hw_matrix) C.int32_t {
		return C.zxv_hw_function_add(cp, mx, C.uint32_t(fnID), n)
	})
}

// AddVector adds a golden vector of the function's current contract (output
// at most 512 bytes). Every implementation of it becomes unproven.
func (m *HwMatrix) AddVector(fnID, op uint32, in, expect []byte) error {
	return m.gated(func(cp *C.zxv_cap, mx *C.zxv_hw_matrix) C.int32_t {
		return C.zxv_hw_vector_add(cp, mx, C.uint32_t(fnID), C.uint32_t(op), bytePtr(in), C.size_t(len(in)),
			bytePtr(expect), C.size_t(len(expect)))
	})
}

// SetContract starts contract version (strictly newer, else ErrRollback):
// the vectors are cleared and every implementation must prove itself again.
func (m *HwMatrix) SetContract(fnID, version uint32) error {
	return m.gated(func(cp *C.zxv_cap, mx *C.zxv_hw_matrix) C.int32_t {
		return C.zxv_hw_function_contract(cp, mx, C.uint32_t(fnID), C.uint32_t(version))
	})
}

// SetPolicy sets placement: policy flags, a latency ceiling and a power
// budget (0: none). Clearing HwPolicyKeyBearing once set: ErrPolicy.
func (m *HwMatrix) SetPolicy(fnID uint32, flags HwPolicy, maxLatencyNs, powerBudgetMw uint32) error {
	return m.gated(func(cp *C.zxv_cap, mx *C.zxv_hw_matrix) C.int32_t {
		return C.zxv_hw_policy_set(cp, mx, C.uint32_t(fnID), C.uint32_t(flags), C.uint32_t(maxLatencyNs),
			C.uint32_t(powerBudgetMw))
	})
}

// FunctionInfo returns a function's contract version, policy and vector count.
func (m *HwMatrix) FunctionInfo(fnID uint32) (HwFunctionInfo, error) {
	var con, pol, nv C.uint32_t
	err := m.gated(func(cp *C.zxv_cap, mx *C.zxv_hw_matrix) C.int32_t {
		return C.zxv_hw_function_info(cp, mx, C.uint32_t(fnID), &con, &pol, &nv)
	})
	return HwFunctionInfo{Contract: uint32(con), Policy: HwPolicy(pol), Vectors: uint32(nv)}, err
}

// AddResource adds a resource (a component implementations run on).
func (m *HwMatrix) AddResource(resID uint32) error {
	return m.gated(func(cp *C.zxv_cap, mx *C.zxv_hw_matrix) C.int32_t {
		return C.zxv_hw_resource_add(cp, mx, C.uint32_t(resID))
	})
}

// SetResource sets a resource's state.
func (m *HwMatrix) SetResource(resID uint32, state HwResourceState) error {
	return m.gated(func(cp *C.zxv_cap, mx *C.zxv_hw_matrix) C.int32_t {
		return C.zxv_hw_resource_set(cp, mx, C.uint32_t(resID), C.int32_t(state))
	})
}

// AddProvider adds an implementation of fnID on resource resID and returns
// its index. It serves only after Attest proves it.
func (m *HwMatrix) AddProvider(fnID, resID uint32, kind HwKind, cost uint32, f HwProviderFunc) (uint32, error) {
	if f == nil {
		return 0, newError(StatusArg, "provider function is nil")
	}
	h := cgo.NewHandle(&hwProvider{f})
	var idx C.uint32_t
	m.mu.Lock()
	defer m.mu.Unlock()
	err := m.gated(func(cp *C.zxv_cap, mx *C.zxv_hw_matrix) C.int32_t {
		return C.zxv_go_hw_provider_add(cp, mx, C.uint32_t(fnID), C.uint32_t(resID), C.int32_t(kind),
			C.uint32_t(cost), C.uintptr_t(h), &idx)
	})
	if err != nil {
		h.Delete()
		return 0, err
	}
	m.cgoh = append(m.cgoh, h)
	return uint32(idx), nil
}

// SetTraits declares an implementation's traits: typical latency per call,
// power while serving, throughput class 0 (slowest) .. 15.
func (m *HwMatrix) SetTraits(provider, latencyNs, powerMw, throughputClass uint32) error {
	return m.gated(func(cp *C.zxv_cap, mx *C.zxv_hw_matrix) C.int32_t {
		return C.zxv_hw_provider_traits(cp, mx, C.uint32_t(provider), C.uint32_t(latencyNs), C.uint32_t(powerMw),
			C.uint32_t(throughputClass))
	})
}

// AddDependency declares that an implementation calls depFnID.
func (m *HwMatrix) AddDependency(provider, depFnID uint32) error {
	return m.gated(func(cp *C.zxv_cap, mx *C.zxv_hw_matrix) C.int32_t {
		return C.zxv_hw_provider_dep(cp, mx, C.uint32_t(provider), C.uint32_t(depFnID))
	})
}

// Enable enables or disables an implementation.
func (m *HwMatrix) Enable(provider uint32, on bool) error {
	v := C.int32_t(0)
	if on {
		v = 1
	}
	return m.gated(func(cp *C.zxv_cap, mx *C.zxv_hw_matrix) C.int32_t {
		return C.zxv_hw_provider_enable(cp, mx, C.uint32_t(provider), v)
	})
}

// Attest runs the golden vectors: TRUE (serves), FALSE (a vector failed)
// or NEITHER (no vectors).
func (m *HwMatrix) Attest(provider uint32) (Truth, error) {
	var lp C.int32_t
	if err := m.gated(func(cp *C.zxv_cap, mx *C.zxv_hw_matrix) C.int32_t {
		return C.zxv_hw_attest(cp, mx, C.uint32_t(provider), &lp)
	}); err != nil {
		return Truth{}, err
	}
	return TruthFromRaw(int(lp))
}

// ProviderCount returns the number of implementations (indexed 0 .. n-1).
func (m *HwMatrix) ProviderCount() (uint32, error) {
	var n C.uint32_t
	err := m.gated(func(cp *C.zxv_cap, mx *C.zxv_hw_matrix) C.int32_t { return C.zxv_hw_provider_count(cp, mx, &n) })
	return uint32(n), err
}

// ProviderInfo returns everything about implementation provider.
func (m *HwMatrix) ProviderInfo(provider uint32) (HwProviderInfo, error) {
	var fn, con, flags, tput, pwr C.uint32_t
	var kind, lp C.int32_t
	var calls, spent C.uint64_t
	err := m.gated(func(cp *C.zxv_cap, mx *C.zxv_hw_matrix) C.int32_t {
		return C.zxv_hw_provider_info(cp, mx, C.uint32_t(provider), &fn, &kind, &lp, &con, &flags, &tput, &pwr,
			&calls, &spent)
	})
	if err != nil {
		return HwProviderInfo{}, err
	}
	t, err := TruthFromRaw(int(lp))
	return HwProviderInfo{FnID: uint32(fn), Kind: HwKind(kind), Proof: t, Contract: uint32(con),
		Flags: HwProviderFlags(flags), ThroughputClass: uint32(tput), Power: HwPower(pwr),
		Calls: uint64(calls), CostSpent: uint64(spent)}, err
}

// Providers returns every implementation, in index order.
func (m *HwMatrix) Providers() ([]HwProviderInfo, error) {
	n, err := m.ProviderCount()
	if err != nil {
		return nil, err
	}
	out := make([]HwProviderInfo, 0, n)
	for i := uint32(0); i < n; i++ {
		pi, err := m.ProviderInfo(i)
		if err != nil {
			return nil, err
		}
		out = append(out, pi)
	}
	return out, nil
}

// FindProvider returns the index of the first implementation of fnID.
func (m *HwMatrix) FindProvider(fnID uint32) (uint32, bool, error) {
	all, err := m.Providers()
	if err != nil {
		return 0, false, err
	}
	for i, p := range all {
		if p.FnID == fnID {
			return uint32(i), true, nil
		}
	}
	return 0, false, nil
}

// Bound returns the implementation serving fnID now (ErrNoProvider if none).
func (m *HwMatrix) Bound(fnID uint32) (uint32, error) {
	var p C.uint32_t
	err := m.gated(func(cp *C.zxv_cap, mx *C.zxv_hw_matrix) C.int32_t {
		return C.zxv_hw_bound(cp, mx, C.uint32_t(fnID), &p)
	})
	return uint32(p), err
}

// Call calls fnID with an output of at most HwCallOutDefault bytes.
func (m *HwMatrix) Call(fnID, op uint32, in []byte) ([]byte, error) {
	out := make([]byte, HwCallOutDefault)
	n, err := m.CallInto(fnID, op, in, out)
	if err != nil {
		return nil, err
	}
	return out[:n], nil
}

// CallInto calls fnID, writing into out; it returns the output length.
func (m *HwMatrix) CallInto(fnID, op uint32, in, out []byte) (int, error) {
	var n C.size_t
	err := m.gated(func(cp *C.zxv_cap, mx *C.zxv_hw_matrix) C.int32_t {
		return C.zxv_hw_call(cp, mx, C.uint32_t(fnID), C.uint32_t(op), bytePtr(in), C.size_t(len(in)),
			bytePtr(out), C.size_t(len(out)), &n)
	})
	return int(n), err
}

// Shadow is Call that also runs the next-best implementation and compares;
// checked reports whether a second one ran. On a mismatch both stop
// serving: ErrDispute.
func (m *HwMatrix) Shadow(fnID, op uint32, in []byte) (out []byte, checked bool, err error) {
	buf := make([]byte, HwCallOutDefault)
	var n C.size_t
	var ck C.int32_t
	err = m.gated(func(cp *C.zxv_cap, mx *C.zxv_hw_matrix) C.int32_t {
		return C.zxv_hw_shadow(cp, mx, C.uint32_t(fnID), C.uint32_t(op), bytePtr(in), C.size_t(len(in)),
			bytePtr(buf), C.size_t(len(buf)), &n, &ck)
	})
	if err != nil {
		return nil, false, err
	}
	return buf[:n], ck != 0, nil
}

// Memo declares fnID pure with a memo of entries (a power of two, at most
// 65536; 0 removes it) and returns its hits and misses. Key-bearing
// functions: ErrPolicy.
func (m *HwMatrix) Memo(fnID, entries uint32) (hits, misses uint64, err error) {
	var h, mi C.uint64_t
	err = m.gated(func(cp *C.zxv_cap, mx *C.zxv_hw_matrix) C.int32_t {
		return C.zxv_hw_memo(cp, mx, C.uint32_t(fnID), C.uint32_t(entries), &h, &mi)
	})
	return uint64(h), uint64(mi), err
}

// TrustKey trusts a release key (HwPKBytes; at most 4) for contract
// packages (needs CapHWReconf).
func (m *HwMatrix) TrustKey(pk []byte) error {
	if err := need(pk, HwPKBytes, "release public key"); err != nil {
		return err
	}
	return m.gated(func(cp *C.zxv_cap, mx *C.zxv_hw_matrix) C.int32_t {
		return C.zxv_hw_matrix_trust_key(cp, mx, bytePtr(pk))
	})
}

// ApplyContract applies a signed HwImgVectors package: it verifies both
// signatures, requires a newer version and svn >= the floor (ErrRollback),
// installs the vectors, proves every implementation again and records it.
// It returns how many implementations prove the new contract (needs
// CapHWReconf).
func (m *HwMatrix) ApplyContract(pkg []byte) (uint32, error) {
	var proven C.uint32_t
	err := m.gated(func(cp *C.zxv_cap, mx *C.zxv_hw_matrix) C.int32_t {
		return C.zxv_hw_contract_apply(cp, mx, bytePtr(pkg), C.size_t(len(pkg)), &proven)
	})
	return uint32(proven), err
}

// Log returns the matrix audit log's head and length (ErrFatal if the
// chain does not verify).
func (m *HwMatrix) Log() (HwLog, error) {
	var l HwLog
	var n C.uint32_t
	err := m.gated(func(cp *C.zxv_cap, mx *C.zxv_hw_matrix) C.int32_t {
		return C.zxv_hw_matrix_log(cp, mx, ptr32(&l.Head), &n)
	})
	l.Count = uint32(n)
	return l, err
}

type logOut struct {
	ev                   C.int32_t
	reason, id, ver, tag C.uint32_t
	subject, hash        [32]byte
}

func (o *logOut) entry() (HwLogEntry, error) {
	tag, err := HexagramFromTag(uint8(o.tag))
	if err != nil || o.tag > 63 {
		return HwLogEntry{}, newError(StatusInternal, "audit tag out of range")
	}
	return HwLogEntry{Event: HwEventKind(o.ev), Reason: uint32(o.reason), ID: uint32(o.id),
		Version: uint32(o.ver), Tag: tag, Subject: o.subject, Hash: o.hash}, nil
}

// LogEntry returns audit record index (0 = oldest).
func (m *HwMatrix) LogEntry(index uint32) (HwLogEntry, error) {
	var o logOut
	if err := m.gated(func(cp *C.zxv_cap, mx *C.zxv_hw_matrix) C.int32_t {
		return C.zxv_hw_matrix_log_entry(cp, mx, C.uint32_t(index), &o.ev, &o.reason, &o.id, &o.ver, &o.tag,
			ptr32(&o.subject), ptr32(&o.hash))
	}); err != nil {
		return HwLogEntry{}, err
	}
	return o.entry()
}

// LogEntries returns every audit record, oldest first.
func (m *HwMatrix) LogEntries() ([]HwLogEntry, error) {
	return logEntries(m.Log, m.LogEntry)
}

func logEntries(head func() (HwLog, error), at func(uint32) (HwLogEntry, error)) ([]HwLogEntry, error) {
	l, err := head()
	if err != nil {
		return nil, err
	}
	out := make([]HwLogEntry, 0, l.Count)
	for i := uint32(0); i < l.Count; i++ {
		e, err := at(i)
		if err != nil {
			return nil, err
		}
		out = append(out, e)
	}
	return out, nil
}

//export zxvGoHwCall
func zxvGoHwCall(h C.uintptr_t, op C.uint32_t, in *C.uint8_t, inLen C.size_t, out *C.uint8_t, outCap C.size_t,
	outLen *C.size_t) (rc C.int32_t) {
	defer func() {
		if recover() != nil {
			rc = C.ZXV_E_INTERNAL
		}
	}()
	p, ok := cgo.Handle(h).Value().(*hwProvider)
	if !ok {
		return C.ZXV_E_INTERNAL
	}
	var input, output []byte
	if in != nil && inLen > 0 {
		input = unsafe.Slice((*byte)(unsafe.Pointer(in)), int(inLen))
	}
	if out != nil && outCap > 0 {
		output = unsafe.Slice((*byte)(unsafe.Pointer(out)), int(outCap))
	}
	n, err := p.f(uint32(op), input, output)
	if err != nil {
		return statusFromError(err)
	}
	if n < 0 || n > len(output) {
		return C.ZXV_E_BUFFER
	}
	if outLen != nil {
		*outLen = C.size_t(n)
	}
	return C.ZXV_OK
}

// ===== Release keys and packages ===========================================

// HwReleaseKey is a release key pair: the public key (HwPKBytes) and both
// secret halves, each a SecretKey wiped on Close.
type HwReleaseKey struct {
	Public []byte     // ML-DSA-87 public key || SLH-DSA public key
	MLDSA  *SecretKey // HwMLDSASKBytes
	SLHDSA *SecretKey // HwSLHDSASKBytes
}

// Close wipes and frees both secret halves.
func (k *HwReleaseKey) Close() {
	if k.MLDSA != nil {
		k.MLDSA.Close()
	}
	if k.SLHDSA != nil {
		k.SLHDSA.Close()
	}
}

// String never prints the secrets.
func (k *HwReleaseKey) String() string {
	return fmt.Sprintf("HwReleaseKey(public %d bytes, secrets redacted)", len(k.Public))
}

// HwImageSpec holds the header fields of a package.
type HwImageSpec struct {
	Kind         HwImageKind
	TargetClass  uint32    // hardware class the image is for (0 for a contract package)
	TargetDigest *[32]byte // target profile digest, nil for zeros
	FnID         uint32    // function the region serves, or whose contract a HwImgVectors package replaces
	Version      uint32
	SVN          uint32 // anti-rollback floor
}

// HwRelease builds release keys and signed packages through a capability
// with CapCryptoSign. SLH-DSA signing takes seconds: a release step.
type HwRelease struct{ cap *Capability }

// NewHwRelease returns packaging through cap.
func NewHwRelease(cap *Capability) *HwRelease { return &HwRelease{cap} }

// HwPackageLen returns the length of a package with a payloadLen-byte payload.
func HwPackageLen(payloadLen int) int { return HwHeaderBytes + payloadLen + HwSigBytes }

// KeyGen returns a fresh release key pair. Close it when done.
func (r *HwRelease) KeyGen() (*HwReleaseKey, error) {
	k := &HwReleaseKey{Public: make([]byte, HwPKBytes), MLDSA: newSecret(HwMLDSASKBytes), SLHDSA: newSecret(HwSLHDSASKBytes)}
	err := k.MLDSA.use(HwMLDSASKBytes, "ML-DSA secret key", func(ms *C.uint8_t) error {
		return k.SLHDSA.use(HwSLHDSASKBytes, "SLH-DSA secret key", func(ss *C.uint8_t) error {
			return r.cap.gated(func(cp *C.zxv_cap) C.int32_t { return C.zxv_hw_keygen(cp, bytePtr(k.Public), ms, ss) })
		})
	})
	if err != nil {
		k.Close()
		return nil, err
	}
	return k, nil
}

// Build builds and signs a package: header || payload || signatures.
func (r *HwRelease) Build(spec HwImageSpec, payload []byte, key *HwReleaseKey) ([]byte, error) {
	if key == nil || key.MLDSA == nil || key.SLHDSA == nil {
		return nil, newError(StatusArg, "release key is nil")
	}
	buf := make([]byte, HwPackageLen(len(payload)))
	var n C.size_t
	var td *C.uint8_t
	if spec.TargetDigest != nil {
		td = ptr32(spec.TargetDigest)
	}
	err := key.MLDSA.use(HwMLDSASKBytes, "ML-DSA secret key", func(ms *C.uint8_t) error {
		return key.SLHDSA.use(HwSLHDSASKBytes, "SLH-DSA secret key", func(ss *C.uint8_t) error {
			return r.cap.gated(func(cp *C.zxv_cap) C.int32_t {
				return C.zxv_hw_image_build(cp, C.uint32_t(spec.Kind), C.uint32_t(spec.TargetClass), td,
					C.uint32_t(spec.FnID), C.uint32_t(spec.Version), C.uint32_t(spec.SVN), bytePtr(payload),
					C.size_t(len(payload)), ms, ss, bytePtr(buf), C.size_t(len(buf)), &n)
			})
		})
	})
	if err != nil {
		return nil, err
	}
	return buf[:n], nil
}

// ===== Regions =============================================================

// HwDevice is the device side of a region: a two-slot reconfigurable unit.
// Each method runs on the goroutine of the region call that needs it, with
// the region (and its matrix) locked. A returned error is reported as its
// status (ZXV_E_INTERNAL without one, and for a panic).
type HwDevice interface {
	// Load loads payload into slot. payload is valid only during the call.
	Load(slot uint32, payload []byte) error
	// Readback returns the SHA-256 of what slot really holds.
	Readback(slot uint32) ([32]byte, error)
	// Activate switches the unit to slot.
	Activate(slot uint32) error
}

// HwEvent is one region event.
type HwEvent struct {
	RegionID uint32
	Kind     HwEventKind
	Reason   uint32 // failure code, 0 for none
	Version  uint32
}

// HwRegionInfo is what HwRegion.Info reports.
type HwRegionInfo struct {
	State       HwRegionState
	Version     uint32 // active image version
	Floor       uint32 // anti-rollback floor
	Quarantined uint32
}

type hwDevice struct{ dev HwDevice }

type hwEvents struct {
	mu  sync.Mutex
	cbs []func(HwEvent)
}

// HwRegion is a reconfigurable region updated with signed images (needs
// CapHWReconf). Stage verifies an image (ML-DSA-87 and SLH-DSA, both
// required) and loads it into the inactive slot, Trial switches to it and
// proves it against the golden vectors of the matrix implementation it
// serves, Commit makes it permanent. Every step is an event and a record in
// a hash-chained audit log; a failing image is rolled back and quarantined
// forever. When the log fills, the region halts and the context is
// poisoned (fail closed). Safe for concurrent use.
type HwRegion struct {
	h      *handle
	cap    *Capability
	events *hwEvents
	devH   cgo.Handle
	evH    cgo.Handle
}

// NewHwRegion creates region regionID for hardware class targetClass. mx
// and provider name the FABRIC or MICRO implementation whose golden vectors
// test each new image (mx nil: no test). targetDigest may be nil (zeros).
// logCap is the audit log size (1 .. 2^20).
func NewHwRegion(cap *Capability, mx *HwMatrix, provider, regionID, targetClass uint32, targetDigest *[32]byte,
	dev HwDevice, logCap uint32) (*HwRegion, error) {
	if dev == nil {
		return nil, newError(StatusArg, "device is nil")
	}
	rg := &HwRegion{cap: cap, events: &hwEvents{}}
	rg.devH = cgo.NewHandle(&hwDevice{dev})
	rg.evH = cgo.NewHandle(rg.events)
	var td *C.uint8_t
	if targetDigest != nil {
		td = ptr32(targetDigest)
	}
	var r *C.zxv_hw_region
	create := func(mxp *C.zxv_hw_matrix) error {
		return cap.gated(func(cp *C.zxv_cap) C.int32_t {
			return C.zxv_go_hw_region_create(cp, mxp, C.uint32_t(provider), C.uint32_t(regionID),
				C.uint32_t(targetClass), td, C.uintptr_t(rg.devH), C.uint32_t(logCap), &r)
		})
	}
	var err error
	parents := []*handle{cap.h}
	if mx != nil {
		err = mx.h.use(func(p unsafe.Pointer) error { return create((*C.zxv_hw_matrix)(p)) })
		parents = append(parents, mx.h)
	} else {
		err = create(nil)
	}
	if err == nil {
		err = cap.gated(func(cp *C.zxv_cap) C.int32_t { return C.zxv_go_hw_region_events(cp, r, C.uintptr_t(rg.evH)) })
		if err != nil {
			C.zxv_hw_region_destroy(r)
		}
	}
	if err != nil {
		rg.devH.Delete()
		rg.evH.Delete()
		return nil, err
	}
	rg.h = newHandle("hardware region", unsafe.Pointer(r), func(p unsafe.Pointer) {
		C.zxv_hw_region_destroy((*C.zxv_hw_region)(p))
		rg.devH.Delete()
		rg.evH.Delete()
	}, parents...)
	runtime.SetFinalizer(rg, func(rg *HwRegion) { _ = rg.h.close() })
	return rg, nil
}

// Close destroys the region.
func (rg *HwRegion) Close() error { return rg.h.close() }

func (rg *HwRegion) gated(f func(cp *C.zxv_cap, r *C.zxv_hw_region) C.int32_t) error {
	return rg.h.use(func(p unsafe.Pointer) error {
		return rg.cap.gated(func(cp *C.zxv_cap) C.int32_t { return f(cp, (*C.zxv_hw_region)(p)) })
	})
}

// OnEvent registers a callback for every event, delivered after each
// recorded step once the region call has released its locks, in order, on
// the goroutine of that call.
func (rg *HwRegion) OnEvent(f func(HwEvent)) {
	rg.events.mu.Lock()
	rg.events.cbs = append(rg.events.cbs, f)
	rg.events.mu.Unlock()
}

// TrustKey trusts a release key (HwPKBytes; at most 4 per region).
func (rg *HwRegion) TrustKey(pk []byte) error {
	if err := need(pk, HwPKBytes, "release public key"); err != nil {
		return err
	}
	return rg.gated(func(cp *C.zxv_cap, r *C.zxv_hw_region) C.int32_t {
		return C.zxv_hw_region_trust_key(cp, r, bytePtr(pk))
	})
}

// Stage verifies an image and loads it into the inactive slot. Refusals
// (ErrVerificationFailed, ErrRollback) are recorded; a read-back mismatch
// is ErrQuarantined and blocks the image.
func (rg *HwRegion) Stage(image []byte) error {
	return rg.gated(func(cp *C.zxv_cap, r *C.zxv_hw_region) C.int32_t {
		return C.zxv_hw_region_stage(cp, r, bytePtr(image), C.size_t(len(image)))
	})
}

// Trial switches to the staged image and proves it; passing starts a
// probation of ticks. A failure is ErrQuarantined with the previous image
// (or S0) back in place.
func (rg *HwRegion) Trial(ticks uint32) error {
	return rg.gated(func(cp *C.zxv_cap, r *C.zxv_hw_region) C.int32_t {
		return C.zxv_hw_region_trial(cp, r, C.uint32_t(ticks))
	})
}

// Commit makes the tested image permanent and raises the floor.
func (rg *HwRegion) Commit() error {
	return rg.gated(func(cp *C.zxv_cap, r *C.zxv_hw_region) C.int32_t { return C.zxv_hw_region_commit(cp, r) })
}

// Rollback returns to the previous image (or S0).
func (rg *HwRegion) Rollback() error {
	return rg.gated(func(cp *C.zxv_cap, r *C.zxv_hw_region) C.int32_t { return C.zxv_hw_region_rollback(cp, r) })
}

// Tick runs one health tick; an expiring probation rolls back
// (ErrQuarantined).
func (rg *HwRegion) Tick() error {
	return rg.gated(func(cp *C.zxv_cap, r *C.zxv_hw_region) C.int32_t { return C.zxv_hw_region_tick(cp, r) })
}

// Info returns the state, active version, floor and quarantine count.
func (rg *HwRegion) Info() (HwRegionInfo, error) {
	var st C.int32_t
	var ver, floor, nq C.uint32_t
	err := rg.gated(func(cp *C.zxv_cap, r *C.zxv_hw_region) C.int32_t {
		return C.zxv_hw_region_info(cp, r, &st, &ver, &floor, &nq)
	})
	return HwRegionInfo{State: HwRegionState(st), Version: uint32(ver), Floor: uint32(floor), Quarantined: uint32(nq)}, err
}

// Log returns the audit log's head and length (ErrFatal if the chain does
// not verify).
func (rg *HwRegion) Log() (HwLog, error) {
	var l HwLog
	var n C.uint32_t
	err := rg.gated(func(cp *C.zxv_cap, r *C.zxv_hw_region) C.int32_t {
		return C.zxv_hw_region_log(cp, r, ptr32(&l.Head), &n)
	})
	l.Count = uint32(n)
	return l, err
}

// LogEntry returns audit record index (0 = oldest).
func (rg *HwRegion) LogEntry(index uint32) (HwLogEntry, error) {
	var o logOut
	if err := rg.gated(func(cp *C.zxv_cap, r *C.zxv_hw_region) C.int32_t {
		return C.zxv_hw_region_log_entry(cp, r, C.uint32_t(index), &o.ev, &o.reason, &o.id, &o.ver, &o.tag,
			ptr32(&o.subject), ptr32(&o.hash))
	}); err != nil {
		return HwLogEntry{}, err
	}
	return o.entry()
}

// LogEntries returns every audit record, oldest first.
func (rg *HwRegion) LogEntries() ([]HwLogEntry, error) {
	return logEntries(rg.Log, rg.LogEntry)
}

func deviceOf(h C.uintptr_t) HwDevice {
	d, ok := cgo.Handle(h).Value().(*hwDevice)
	if !ok {
		return nil
	}
	return d.dev
}

//export zxvGoHwLoad
func zxvGoHwLoad(h C.uintptr_t, slot C.uint32_t, payload *C.uint8_t, n C.size_t) (rc C.int32_t) {
	defer func() {
		if recover() != nil {
			rc = C.ZXV_E_INTERNAL
		}
	}()
	d := deviceOf(h)
	if d == nil {
		return C.ZXV_E_INTERNAL
	}
	var p []byte
	if payload != nil && n > 0 {
		p = unsafe.Slice((*byte)(unsafe.Pointer(payload)), int(n))
	}
	if err := d.Load(uint32(slot), p); err != nil {
		return statusFromError(err)
	}
	return C.ZXV_OK
}

//export zxvGoHwReadback
func zxvGoHwReadback(h C.uintptr_t, slot C.uint32_t, digest *C.uint8_t) (rc C.int32_t) {
	defer func() {
		if recover() != nil {
			rc = C.ZXV_E_INTERNAL
		}
	}()
	d := deviceOf(h)
	if d == nil || digest == nil {
		return C.ZXV_E_INTERNAL
	}
	sum, err := d.Readback(uint32(slot))
	if err != nil {
		return statusFromError(err)
	}
	copy(unsafe.Slice((*byte)(unsafe.Pointer(digest)), 32), sum[:])
	return C.ZXV_OK
}

//export zxvGoHwActivate
func zxvGoHwActivate(h C.uintptr_t, slot C.uint32_t) (rc C.int32_t) {
	defer func() {
		if recover() != nil {
			rc = C.ZXV_E_INTERNAL
		}
	}()
	d := deviceOf(h)
	if d == nil {
		return C.ZXV_E_INTERNAL
	}
	if err := d.Activate(uint32(slot)); err != nil {
		return statusFromError(err)
	}
	return C.ZXV_OK
}

//export zxvGoHwEvent
func zxvGoHwEvent(h C.uintptr_t, regionID C.uint32_t, event C.int32_t, reason, version C.uint32_t) {
	defer func() { _ = recover() }()
	ev, ok := cgo.Handle(h).Value().(*hwEvents)
	if !ok {
		return
	}
	ev.mu.Lock()
	cbs := append([]func(HwEvent){}, ev.cbs...)
	ev.mu.Unlock()
	e := HwEvent{RegionID: uint32(regionID), Kind: HwEventKind(event), Reason: uint32(reason), Version: uint32(version)}
	for _, cb := range cbs {
		cb(e)
	}
}
