// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.evm;

import java.util.HexFormat;

/**
 * One state-channel party: a 20-byte Ethereum address and a signed balance
 * in exact minor units.
 *
 * @param address       20 bytes
 * @param balanceMinor  the balance (negative allowed; encoded as int256)
 */
public record Party(byte[] address, long balanceMinor) {
    /** Validates the address length. */
    public Party {
        if (address == null || address.length != 20) throw new IllegalArgumentException("an EVM address is 20 bytes");
        address = address.clone();
    }

    /** A party from a hex address ({@code 0x} prefix optional). */
    public static Party of(String hexAddress, long balanceMinor) {
        String h = hexAddress.startsWith("0x") || hexAddress.startsWith("0X") ? hexAddress.substring(2) : hexAddress;
        return new Party(HexFormat.of().parseHex(h), balanceMinor);
    }
}
