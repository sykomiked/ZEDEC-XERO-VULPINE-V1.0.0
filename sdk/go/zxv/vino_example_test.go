// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv_test

import (
	"errors"
	"fmt"

	"github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0/sdk/go/zxv"
)

// A play-lane shop: an issuer backs, issues a voucher to a player, the
// player spends it at the shop, and the shop redeems it. Play units are not
// money, and redeeming retires the voucher without paying anything out.
func ExampleVinoPlay() {
	ctx, _ := zxv.NewContext()
	defer ctx.Close()
	root, _ := ctx.RootCapability(1, nil, zxv.CapVino)
	defer root.Close()

	play, _ := zxv.NewVinoPlay(root)
	defer play.Close()
	issuer, _ := play.Open(100, zxv.VinoIssuer)
	player, _ := play.Open(200, zxv.VinoHolder)
	shop, _ := play.Open(300, zxv.VinoHolder)

	_ = play.Back(issuer, 18) // 10*18 >= 18*10: room for faces up to 10
	_, err := play.Issue(issuer, player, 13, zxv.VinoBearer)
	fmt.Println(errors.Is(err, zxv.ErrCoverage)) // 13 would break 1.8x coverage

	coin, _ := play.Issue(issuer, player, 8, zxv.VinoBearer)
	err = play.Transfer(coin, player, shop, zxv.VinoGhost)
	fmt.Println(errors.Is(err, zxv.ErrRail)) // must spend on its active rail
	_ = play.Transfer(coin, player, shop, zxv.VinoBearer)
	_ = play.Redeem(coin, shop)

	v, _ := play.Voucher(coin)
	acct, _ := play.Account(issuer)
	fmt.Println(v.State, acct.Backing, acct.Faces)
	// Output:
	// true
	// true
	// REDEEMED 18 0
}

// A live payment: the app builds a request, the user's wallet signs a
// receipt (wallet side, its own key), and the app verifies it offline.
func ExampleVerifyVinoReceipt() {
	ctx, _ := zxv.NewContext()
	defer ctx.Close()
	root, _ := ctx.RootCapability(1, nil, zxv.CapCryptoSign)
	defer root.Close()

	// App side: a request for 1234567 VFV minor units, valid for 10 minutes.
	req := zxv.VinoRequest{Lane: zxv.VinoLaneLive, Amount: 1234567,
		CreatedAt: 1700000000, ExpiresAt: 1700000600, Memo: "Sword of Dawn"}
	req.Merchant[0], req.Nonce[0] = 1, 0xa0
	rec, id, _ := req.Encode()

	// Wallet side (in the user's wallet, never in the app).
	walletPK, walletSK, _ := zxv.NewMLDSA87(root).KeyGenFromSeed([32]byte{0x31})
	defer walletSK.Close()
	sig, _ := zxv.SignVinoReceipt(root, rec, walletSK, walletPK, 1700000100)

	// App side again: offline, no capability.
	fmt.Println(zxv.VerifyVinoReceipt(rec, walletPK, 1700000100, sig, 1700000200))
	err := zxv.VerifyVinoReceipt(rec, walletPK, 1700000100, sig, 1700000601)
	fmt.Println(errors.Is(err, zxv.ErrExpired))
	back, id2, _ := zxv.DecodeVinoRequest(rec)
	fmt.Println(back.Memo, id2 == id)
	// Output:
	// <nil>
	// true
	// Sword of Dawn true
}
