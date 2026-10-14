// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// hwkey_se.swift — the Apple Secure Enclave for hwkey_se.c, through
// CryptoKit. The enclave key is a P-256 key-agreement key; its private half
// never leaves the enclave. What the host keeps is the key's
// dataRepresentation: a blob encrypted by the enclave that only this
// machine's enclave can use. Keeping it in the sealed key file instead of
// the keychain means an ad-hoc signed app needs no keychain entitlement.
//
// Each function returns 0 on success, -3 when there is no enclave, and
// another negative value on failure. Z is CryptoKit's raw shared secret
// (the x coordinate, 32 bytes); E is the ephemeral public key in X9.63
// form (04 || x || y, 65 bytes).

import CryptoKit
import Foundation

@_cdecl("zxv_se_available")
public func zxv_se_available() -> Int32 {
    return SecureEnclave.isAvailable ? 1 : 0
}

@_cdecl("zxv_se_create")
public func zxv_se_create(_ out: UnsafeMutablePointer<UInt8>,
                          _ outLen: UnsafeMutablePointer<UInt32>) -> Int32 {
    guard SecureEnclave.isAvailable else { return -3 }
    do {
        let key = try SecureEnclave.P256.KeyAgreement.PrivateKey()
        let blob = key.dataRepresentation
        guard blob.count <= Int(outLen.pointee) else { return -1 }
        blob.copyBytes(to: out, count: blob.count)
        outLen.pointee = UInt32(blob.count)
        return 0
    } catch {
        return -2
    }
}

private func copySecret(_ s: SharedSecret, _ z: UnsafeMutablePointer<UInt8>) -> Int32 {
    return s.withUnsafeBytes { (raw: UnsafeRawBufferPointer) -> Int32 in
        guard raw.count == 32 else { return -1 }
        for i in 0..<32 { z[i] = raw[i] }
        return 0
    }
}

@_cdecl("zxv_se_wrap")
public func zxv_se_wrap(_ handle: UnsafePointer<UInt8>, _ handleLen: UInt32,
                        _ eph: UnsafeMutablePointer<UInt8>,
                        _ ephLen: UnsafeMutablePointer<UInt32>,
                        _ z: UnsafeMutablePointer<UInt8>) -> Int32 {
    guard SecureEnclave.isAvailable else { return -3 }
    guard ephLen.pointee >= 65 else { return -1 }
    do {
        let key = try SecureEnclave.P256.KeyAgreement.PrivateKey(
            dataRepresentation: Data(bytes: handle, count: Int(handleLen)))
        let e = P256.KeyAgreement.PrivateKey()
        let pub = e.publicKey.x963Representation
        guard pub.count == 65 else { return -1 }
        let s = try key.sharedSecretFromKeyAgreement(with: e.publicKey)
        pub.copyBytes(to: eph, count: 65)
        ephLen.pointee = 65
        return copySecret(s, z)
    } catch {
        return -2
    }
}

@_cdecl("zxv_se_unwrap")
public func zxv_se_unwrap(_ handle: UnsafePointer<UInt8>, _ handleLen: UInt32,
                          _ eph: UnsafePointer<UInt8>, _ ephLen: UInt32,
                          _ z: UnsafeMutablePointer<UInt8>) -> Int32 {
    guard SecureEnclave.isAvailable else { return -3 }
    do {
        let key = try SecureEnclave.P256.KeyAgreement.PrivateKey(
            dataRepresentation: Data(bytes: handle, count: Int(handleLen)))
        let pub = try P256.KeyAgreement.PublicKey(
            x963Representation: Data(bytes: eph, count: Int(ephLen)))
        let s = try key.sharedSecretFromKeyAgreement(with: pub)
        return copySecret(s, z)
    } catch {
        return -2
    }
}
