// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

// Command quickstart shows a context, capabilities, exact money,
// four-valued logic and post-quantum signing.
//
//	go run ./examples/quickstart
package main

import (
	"errors"
	"fmt"
	"log"

	"github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0/sdk/go/zxv"
)

func main() {
	if err := run(); err != nil {
		log.Fatal(err)
	}
}

func run() error {
	major, minor, patch := zxv.Version()
	fmt.Printf("libzxv ABI %d.%d.%d\n", major, minor, patch)

	ctx, err := zxv.NewContext()
	if err != nil {
		return err
	}
	defer ctx.Close()
	root, err := ctx.RootCapability(1, nil, zxv.CapAll)
	if err != nil {
		return err
	}
	defer root.Close()
	reader, err := root.Attenuate(zxv.CapLedgerRead) // can never post
	if err != nil {
		return err
	}
	defer reader.Close()

	if err := ctx.EnableCurrencyCap(root, "NGN"); err != nil {
		return err
	}
	ledger := zxv.NewLedger(root)
	bank, err := ledger.OpenAccount("bank", "NGN", zxv.Issuer)
	if err != nil {
		return err
	}
	alice, err := ledger.OpenAccount("alice", "NGN", zxv.Holder)
	if err != nil {
		return err
	}
	if _, err := ledger.Post(bank, alice, 12550, "NGN", "E2E-0001"); err != nil {
		return err
	}
	bal, err := zxv.NewLedger(reader).Balance(alice)
	if err != nil {
		return err
	}
	fmt.Printf("alice holds %d kobo\n", bal.Equity)

	_, err = zxv.NewLedger(reader).Post(alice, bank, 1, "NGN", "E2E-0002")
	var e *zxv.Error
	if errors.Is(err, zxv.ErrCapabilityDenied) && errors.As(err, &e) {
		fmt.Printf("reader cannot post: %s\n", e.Name)
	}

	a, _ := zxv.ParseRational("0.1")
	b, _ := zxv.ParseRational("0.2")
	sum, err := a.Add(b)
	if err != nil {
		return err
	}
	exact, _ := zxv.FromFloat64Exact(0.1)
	fmt.Printf("0.1 + 0.2 = %v; the double 0.1 is exactly %v\n", sum, exact)

	verdict := zxv.TruthFromEvidence(2, 1)
	fmt.Printf("evidence 2 for, 1 against: %v (certain? %v)\n", verdict, verdict.IsCertain())

	dsa := zxv.NewMLDSA87(root)
	pk, sk, err := dsa.KeyGen()
	if err != nil {
		return err
	}
	defer sk.Close() // wipes the off-heap secret key
	msg := []byte("agent 7: settle cycle 42")
	sig, err := dsa.Sign(sk, msg, []byte("zxv-agent"))
	if err != nil {
		return err
	}
	if err := dsa.Verify(pk, msg, []byte("zxv-agent"), sig); err != nil {
		return err
	}
	fmt.Printf("ML-DSA-87 signature verified (%d bytes)\n", len(sig))
	return nil
}
