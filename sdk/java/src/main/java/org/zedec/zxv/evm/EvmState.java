// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.evm;

import java.util.HexFormat;

/**
 * A state-channel state, ABI-encoded for {@code ZxvStateChannel.sol}.
 *
 * @param encoding {@code abi.encode(bytes32 channelId, uint256 nonce, address[] parties, int256[] balances)}
 * @param digest   {@code keccak256(encoding)}, the 32 bytes the parties sign (EIP-191) in their own wallets
 */
public record EvmState(byte[] encoding, byte[] digest) {
    /** The digest as {@code 0x}-prefixed hex. */
    public String digestHex() {
        return "0x" + HexFormat.of().formatHex(digest);
    }
}
