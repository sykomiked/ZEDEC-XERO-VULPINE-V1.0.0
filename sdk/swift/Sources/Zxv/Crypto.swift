// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import CZxv

/// SHA3-256, SHAKE256 and Ethereum Keccak-256 (pure functions).
public enum Hashes {
    /// SHA3-256.
    public static func sha3_256(_ data: [UInt8]) -> [UInt8] {
        var out = [UInt8](repeating: 0, count: 32)
        _ = zxv_sha3_256(data, data.count, &out)
        return out
    }
    /// SHA3-256 of a string's UTF-8 bytes.
    public static func sha3_256(_ text: String) -> [UInt8] { sha3_256(Array(text.utf8)) }
    /// Ethereum's Keccak-256 (pre-FIPS padding).
    public static func keccak256(_ data: [UInt8]) -> [UInt8] {
        var out = [UInt8](repeating: 0, count: 32)
        _ = zxv_keccak256(data, data.count, &out)
        return out
    }
    /// Keccak-256 of a string's UTF-8 bytes.
    public static func keccak256(_ text: String) -> [UInt8] { keccak256(Array(text.utf8)) }
    /// SHAKE256 with `outputLength` bytes.
    public static func shake256(_ data: [UInt8], outputLength: Int) -> [UInt8] {
        var out = [UInt8](repeating: 0, count: outputLength)
        _ = zxv_shake256(data, data.count, &out, outputLength)
        return out
    }
}

/// `count` bytes from the OS CSPRNG.
public func randomBytes(_ count: Int) throws(ZxvError) -> [UInt8] {
    var b = [UInt8](repeating: 0, count: count)
    try check(zxv_random(&b, count))
    return b
}

/// Lower-case hex of bytes.
public func hexString(_ bytes: some Sequence<UInt8>) -> String {
    let digits = Array("0123456789abcdef")
    var s = ""
    for b in bytes {
        s.append(digits[Int(b >> 4)])
        s.append(digits[Int(b & 15)])
    }
    return s
}

/// Bytes of hex text; nil if malformed.
public func bytes(hex: String) -> [UInt8]? {
    let u = Array(hex.utf8)
    guard u.count % 2 == 0 else { return nil }
    func nib(_ c: UInt8) -> UInt8? {
        switch c {
        case 48...57: return c - 48
        case 97...102: return c - 87
        case 65...70: return c - 55
        default: return nil
        }
    }
    var out = [UInt8]()
    out.reserveCapacity(u.count / 2)
    var i = 0
    while i < u.count {
        guard let a = nib(u[i]), let b = nib(u[i + 1]) else { return nil }
        out.append(a << 4 | b)
        i += 2
    }
    return out
}

/// Secret key material (ML-KEM decapsulation keys, ML-DSA and Ed25519
/// secret keys, shared secrets). The bytes live in a malloc'd buffer
/// outside Swift's copy-on-write arrays and are wiped with
/// `zxv_secure_zero` in `deinit` (or `wipe()`). Read them through
/// `withUnsafeBytes`; `exportBytesUnsafe()` makes a copy that is NOT wiped.
public final class SecretKey: @unchecked Sendable {
    private let buffer: UnsafeMutableRawBufferPointer

    /// An all-zero key of `count` bytes.
    public init(count: Int) {
        self.count = count
        buffer = UnsafeMutableRawBufferPointer.allocate(byteCount: max(count, 1), alignment: 16)
        buffer.initializeMemory(as: UInt8.self, repeating: 0)
    }

    /// A key holding a copy of `bytes` (the caller should wipe its source).
    public convenience init(copying bytes: [UInt8]) {
        self.init(count: bytes.count)
        buffer.copyBytes(from: bytes)
    }

    deinit {
        wipe()
        buffer.deallocate()
    }

    /// Size in bytes.
    public let count: Int

    /// Read the key without copying it.
    public func withUnsafeBytes<R>(_ body: (UnsafeRawBufferPointer) throws -> R) rethrows -> R {
        try body(UnsafeRawBufferPointer(rebasing: buffer[0..<count]))
    }

    var mutablePointer: UnsafeMutablePointer<UInt8> { buffer.baseAddress!.assumingMemoryBound(to: UInt8.self) }
    var pointer: UnsafePointer<UInt8> { UnsafePointer(mutablePointer) }

    /// A heap copy. DEFEATS WIPING: the copy is yours to erase.
    public func exportBytesUnsafe() -> [UInt8] { Array(UnsafeRawBufferPointer(rebasing: buffer[0..<count])) }

    /// Wipe now (the key becomes all zeros).
    public func wipe() { _ = zxv_secure_zero(buffer.baseAddress, buffer.count) }
}

private func needCount(_ n: Int, _ expected: UInt32, _ what: String) throws(ZxvError) {
    if n != Int(expected) {
        throw .usage(ZxvUsageError(status: ZXV_E_ARG, detail: "\(what) must be \(expected) bytes, got \(n)"))
    }
}

/// An ML-KEM-1024 key pair.
public struct KemKeyPair: Sendable {
    /// Public, 1568 bytes.
    public let encapsulationKey: [UInt8]
    /// Secret, 3168 bytes, wiped on release.
    public let decapsulationKey: SecretKey
}

/// The result of encapsulation.
public struct Encapsulation: Sendable {
    /// 1568 bytes: send to the key owner.
    public let ciphertext: [UInt8]
    /// 32 bytes, wiped on release.
    public let sharedSecret: SecretKey
}

/// FIPS 203 ML-KEM-1024 (needs `.cryptoKem`). Not FIPS 140-3 validated.
public struct MlKem1024: Sendable {
    private let cap: Capability
    /// Acting with `cap`.
    public init(_ cap: Capability) { self.cap = cap }

    /// A fresh key pair.
    public func keygen() throws(ZxvError) -> KemKeyPair {
        var ek = [UInt8](repeating: 0, count: Int(ZXV_MLKEM1024_EK_BYTES))
        let dk = SecretKey(count: Int(ZXV_MLKEM1024_DK_BYTES))
        try cap.call { zxv_mlkem1024_keygen($0, &ek, dk.mutablePointer) }
        return KemKeyPair(encapsulationKey: ek, decapsulationKey: dk)
    }
    /// A deterministic key pair from d and z (32 bytes each), for KATs.
    public func keygen(d: [UInt8], z: [UInt8]) throws(ZxvError) -> KemKeyPair {
        try needCount(d.count, 32, "d")
        try needCount(z.count, 32, "z")
        var ek = [UInt8](repeating: 0, count: Int(ZXV_MLKEM1024_EK_BYTES))
        let dk = SecretKey(count: Int(ZXV_MLKEM1024_DK_BYTES))
        try cap.call { zxv_mlkem1024_keygen_derand($0, d, z, &ek, dk.mutablePointer) }
        return KemKeyPair(encapsulationKey: ek, decapsulationKey: dk)
    }
    /// Encapsulate to `encapsulationKey`; a malformed key is a usage error.
    public func encapsulate(to encapsulationKey: [UInt8]) throws(ZxvError) -> Encapsulation {
        try needCount(encapsulationKey.count, ZXV_MLKEM1024_EK_BYTES, "encapsulation key")
        var ct = [UInt8](repeating: 0, count: Int(ZXV_MLKEM1024_CT_BYTES))
        let ss = SecretKey(count: Int(ZXV_MLKEM_SS_BYTES))
        try cap.call { zxv_mlkem1024_encaps($0, encapsulationKey, &ct, ss.mutablePointer) }
        return Encapsulation(ciphertext: ct, sharedSecret: ss)
    }
    /// Deterministic encapsulation with message `m` (32 bytes), for KATs.
    public func encapsulate(to encapsulationKey: [UInt8], m: [UInt8]) throws(ZxvError) -> Encapsulation {
        try needCount(encapsulationKey.count, ZXV_MLKEM1024_EK_BYTES, "encapsulation key")
        try needCount(m.count, 32, "m")
        var ct = [UInt8](repeating: 0, count: Int(ZXV_MLKEM1024_CT_BYTES))
        let ss = SecretKey(count: Int(ZXV_MLKEM_SS_BYTES))
        try cap.call { zxv_mlkem1024_encaps_derand($0, encapsulationKey, m, &ct, ss.mutablePointer) }
        return Encapsulation(ciphertext: ct, sharedSecret: ss)
    }
    /// Recover the shared secret.
    public func decapsulate(_ ciphertext: [UInt8], with dk: SecretKey) throws(ZxvError) -> SecretKey {
        try needCount(dk.count, ZXV_MLKEM1024_DK_BYTES, "decapsulation key")
        try needCount(ciphertext.count, ZXV_MLKEM1024_CT_BYTES, "ciphertext")
        let ss = SecretKey(count: Int(ZXV_MLKEM_SS_BYTES))
        try cap.call { zxv_mlkem1024_decaps($0, dk.pointer, ciphertext, ss.mutablePointer) }
        return ss
    }
}

/// A signature key pair.
public struct SigningKeyPair: Sendable {
    /// Share freely.
    public let publicKey: [UInt8]
    /// Wiped on release.
    public let secretKey: SecretKey
}

/// FIPS 204 ML-DSA-87 (needs `.cryptoSign`). Not FIPS 140-3 validated.
public struct MlDsa87: Sendable {
    private let cap: Capability
    /// Acting with `cap`.
    public init(_ cap: Capability) { self.cap = cap }

    /// A fresh key pair.
    public func keygen() throws(ZxvError) -> SigningKeyPair {
        var pk = [UInt8](repeating: 0, count: Int(ZXV_MLDSA87_PK_BYTES))
        let sk = SecretKey(count: Int(ZXV_MLDSA87_SK_BYTES))
        try cap.call { zxv_mldsa87_keygen($0, &pk, sk.mutablePointer) }
        return SigningKeyPair(publicKey: pk, secretKey: sk)
    }
    /// A key pair from a 32-byte seed.
    public func keygen(seed: [UInt8]) throws(ZxvError) -> SigningKeyPair {
        try needCount(seed.count, 32, "seed")
        var pk = [UInt8](repeating: 0, count: Int(ZXV_MLDSA87_PK_BYTES))
        let sk = SecretKey(count: Int(ZXV_MLDSA87_SK_BYTES))
        try cap.call { zxv_mldsa87_keygen_seed($0, seed, &pk, sk.mutablePointer) }
        return SigningKeyPair(publicKey: pk, secretKey: sk)
    }
    /// Sign: hedged (fresh randomness) by default, or deterministic for KATs.
    /// `context` is the FIPS 204 context string (<= 255 bytes).
    public func sign(_ message: [UInt8], with sk: SecretKey, context: [UInt8] = [],
                     deterministic: Bool = false) throws(ZxvError) -> [UInt8] {
        try needCount(sk.count, ZXV_MLDSA87_SK_BYTES, "secret key")
        var sig = [UInt8](repeating: 0, count: Int(ZXV_MLDSA87_SIG_BYTES))
        let ctx: [UInt8]? = context.isEmpty ? nil : context
        try cap.call {
            deterministic
                ? zxv_mldsa87_sign_deterministic($0, sk.pointer, message, message.count, ctx, context.count, &sig)
                : zxv_mldsa87_sign($0, sk.pointer, message, message.count, ctx, context.count, &sig)
        }
        return sig
    }
    /// True if valid; false if the signature does not verify.
    public func verify(_ signature: [UInt8], of message: [UInt8], publicKey: [UInt8],
                       context: [UInt8] = []) throws(ZxvError) -> Bool {
        try needCount(publicKey.count, ZXV_MLDSA87_PK_BYTES, "public key")
        try needCount(signature.count, ZXV_MLDSA87_SIG_BYTES, "signature")
        try cap.context.ensureAlive()
        let st = zxv_mldsa87_verify(cap.handle, publicKey, message, message.count,
                                    context.isEmpty ? nil : context, context.count, signature)
        if st == ZXV_E_VERIFY { return false }
        try check(st, cap.context)
        return true
    }
}

/// RFC 8032 Ed25519 (needs `.cryptoSign`).
public struct Ed25519: Sendable {
    private let cap: Capability
    /// Acting with `cap`.
    public init(_ cap: Capability) { self.cap = cap }

    /// A fresh key pair.
    public func keygen() throws(ZxvError) -> SigningKeyPair {
        var pk = [UInt8](repeating: 0, count: 32)
        let sk = SecretKey(count: 32)
        try cap.call { zxv_ed25519_keygen($0, &pk, sk.mutablePointer) }
        return SigningKeyPair(publicKey: pk, secretKey: sk)
    }
    /// The public key of a secret key.
    public func publicKey(of sk: SecretKey) throws(ZxvError) -> [UInt8] {
        try needCount(sk.count, ZXV_ED25519_SK_BYTES, "secret key")
        var pk = [UInt8](repeating: 0, count: 32)
        try cap.call { zxv_ed25519_public($0, sk.pointer, &pk) }
        return pk
    }
    /// Sign a message.
    public func sign(_ message: [UInt8], with sk: SecretKey) throws(ZxvError) -> [UInt8] {
        try needCount(sk.count, ZXV_ED25519_SK_BYTES, "secret key")
        var sig = [UInt8](repeating: 0, count: 64)
        try cap.call { zxv_ed25519_sign($0, sk.pointer, message, message.count, &sig) }
        return sig
    }
    /// True if valid; false if the signature does not verify.
    public func verify(_ signature: [UInt8], of message: [UInt8], publicKey: [UInt8]) throws(ZxvError) -> Bool {
        try needCount(publicKey.count, ZXV_ED25519_PK_BYTES, "public key")
        try needCount(signature.count, ZXV_ED25519_SIG_BYTES, "signature")
        try cap.context.ensureAlive()
        let st = zxv_ed25519_verify(cap.handle, publicKey, message, message.count, signature)
        if st == ZXV_E_VERIFY { return false }
        try check(st, cap.context)
        return true
    }
}
