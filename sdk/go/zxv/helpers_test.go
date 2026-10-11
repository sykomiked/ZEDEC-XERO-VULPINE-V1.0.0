// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv_test

import (
	"encoding/hex"
	"encoding/json"
	"os"
	"path/filepath"
	"testing"

	"github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0/sdk/go/zxv"
)

var conformanceDir = filepath.Join("..", "..", "conformance")

func fixtures() string { return filepath.Join(conformanceDir, "fixtures") }

// vectors mirrors sdk/conformance/vectors.json.
type vectors struct {
	ABI    []int `json:"abi"`
	Status []struct {
		Code  int32  `json:"code"`
		Name  string `json:"name"`
		Class int    `json:"class"`
	} `json:"status"`
	RationalOps []struct {
		A, B, Add, Sub, Mul, Div [2]int64
		Cmp                      int
	} `json:"rational_ops"`
	RationalParse []struct {
		Text   string   `json:"text"`
		Status int32    `json:"status"`
		Value  [2]int64 `json:"value"`
		Format string   `json:"format"`
	} `json:"rational_parse"`
	IEEE754 []struct {
		Bits   string   `json:"bits"`
		Status int32    `json:"status"`
		Value  [2]int64 `json:"value"`
	} `json:"ieee754"`
	Q16 []struct {
		Value  [2]int64 `json:"value"`
		Status int32    `json:"status"`
		Q16    int32    `json:"q16"`
	} `json:"q16"`
	LPRes struct {
		Names []string `json:"names"`
		Not   []int    `json:"not"`
		And   [][]int  `json:"and"`
		Or    [][]int  `json:"or"`
	} `json:"lpres"`
	UBH168 []struct {
		Trunk, Shell, Sync uint32
		Tag                uint8
		SPlus              [3]uint32 `json:"s_plus"`
		SMinus             [2]uint32 `json:"s_minus"`
		Frame              string
		Truth              int32
	} `json:"ubh168"`
	UBH168Idle []string `json:"ubh168_idle"`
	Hash       []struct {
		UTF8      string `json:"utf8"`
		Keccak256 string `json:"keccak256"`
		SHA3256   string `json:"sha3_256"`
		SHAKE64   string `json:"shake256_64"`
	} `json:"hash"`
	Ed25519 struct {
		SK, PK, Sig string
		Msg         string `json:"msg_utf8"`
	} `json:"ed25519"`
	MLDSA87 struct {
		Seed    string `json:"seed"`
		Ctx     string `json:"ctx_utf8"`
		Msg     string `json:"msg_utf8"`
		PKHash  string `json:"pk_sha3_256"`
		SKHash  string `json:"sk_sha3_256"`
		SigHash string `json:"sig_deterministic_sha3_256"`
	} `json:"mldsa87"`
	MLKEM1024 struct {
		D, Z, M string
		EKHash  string `json:"ek_sha3_256"`
		CTHash  string `json:"ct_sha3_256"`
		SS      string `json:"ss"`
	} `json:"mlkem1024"`
	Swarm struct {
		Levels         uint32      `json:"levels"`
		TokensPerCycle uint64      `json:"tokens_per_cycle"`
		Models         [][2]uint32 `json:"models"`
		Mood           [2]uint32   `json:"mood"`
		Feelings       [][3]uint32 `json:"feelings"`
		Allotments     [][3]uint64 `json:"allotments"`
		LevelCapacity  []uint32    `json:"level_capacity"`
	} `json:"swarm"`
	Generate struct {
		Model        string  `json:"model"`
		Tokenizer    string  `json:"tokenizer"`
		Prompt       string  `json:"prompt"`
		NCtx         uint32  `json:"n_ctx"`
		PromptTokens []int32 `json:"prompt_tokens"`
		Greedy       struct {
			MaxTokens uint32  `json:"max_tokens"`
			Tokens    []int32 `json:"tokens"`
		} `json:"greedy"`
		Sampled struct {
			MaxTokens        uint32  `json:"max_tokens"`
			TemperatureQ16   int64   `json:"temperature_q16"`
			TopK             uint32  `json:"top_k"`
			TopPQ16          int64   `json:"top_p_q16"`
			RepeatPenaltyQ16 int64   `json:"repeat_penalty_q16"`
			RepeatLastN      uint32  `json:"repeat_last_n"`
			Seed             uint64  `json:"seed"`
			Tokens           []int32 `json:"tokens"`
		} `json:"sampled"`
	} `json:"generate"`
	EVM struct {
		ChannelID string   `json:"channel_id"`
		Nonce     uint64   `json:"nonce"`
		Addresses []string `json:"addresses"`
		Balances  []int64  `json:"balances"`
		Encoding  string   `json:"encoding"`
		Digest    string   `json:"digest"`
	} `json:"evm_state"`
	PhaseCycle uint64 `json:"phase_cycle"`
}

func loadVectors(t *testing.T) *vectors {
	t.Helper()
	b, err := os.ReadFile(filepath.Join(conformanceDir, "vectors.json"))
	if err != nil {
		t.Fatal(err)
	}
	var v vectors
	if err := json.Unmarshal(b, &v); err != nil {
		t.Fatal(err)
	}
	return &v
}

func unhex(t *testing.T, s string) []byte {
	t.Helper()
	b, err := hex.DecodeString(s)
	if err != nil {
		t.Fatal(err)
	}
	return b
}

func arr32(t *testing.T, s string) (a [32]byte) {
	copy(a[:], unhex(t, s))
	return
}

func rat(t *testing.T, p [2]int64) zxv.Rational {
	t.Helper()
	r, err := zxv.NewRational(p[0], p[1])
	if err != nil {
		t.Fatal(err)
	}
	return r
}

func must[T any](t *testing.T) func(T, error) T {
	return func(v T, err error) T {
		t.Helper()
		if err != nil {
			t.Fatal(err)
		}
		return v
	}
}

func ok(t *testing.T, err error) {
	t.Helper()
	if err != nil {
		t.Fatal(err)
	}
}

// env opens a context with an all-powerful root capability.
func env(t *testing.T) (*zxv.Context, *zxv.Capability) {
	t.Helper()
	ctx := must[*zxv.Context](t)(zxv.NewContext())
	cap := must[*zxv.Capability](t)(ctx.RootCapability(1, nil, zxv.CapAll))
	t.Cleanup(func() {
		if err := cap.Close(); err != nil {
			t.Errorf("cap close: %v", err)
		}
		if err := ctx.Close(); err != nil {
			t.Errorf("ctx close: %v", err)
		}
	})
	return ctx, cap
}
