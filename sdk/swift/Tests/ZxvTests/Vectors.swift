// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import Foundation
import Zxv

extension Error {
    /// This error as a ZxvError (usage errors from pure functions included).
    var zxv: ZxvError? {
        if let e = self as? ZxvError { return e }
        if let e = self as? ZxvUsageError { return .usage(e) }
        return nil
    }
}

/// sdk/conformance, found from this file's location (or ZXV_CONFORMANCE_DIR).
let conformanceDir: String = {
    if let d = ProcessInfo.processInfo.environment["ZXV_CONFORMANCE_DIR"] { return d }
    return URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        .deletingLastPathComponent().appendingPathComponent("conformance").path
}()

/// The shared answer key, sdk/conformance/vectors.json.
nonisolated(unsafe) let vectors: [String: Any] = {
    let data = try! Data(contentsOf: URL(fileURLWithPath: "\(conformanceDir)/vectors.json"))
    return try! JSONSerialization.jsonObject(with: data) as! [String: Any]
}()

extension Dictionary where Key == String, Value == Any {
    func obj(_ k: String) -> [String: Any] { self[k] as! [String: Any] }
    func arr(_ k: String) -> [Any] { self[k] as! [Any] }
    func str(_ k: String) -> String { self[k] as! String }
    func int(_ k: String) -> Int64 { (self[k] as! NSNumber).int64Value }
    func uint(_ k: String) -> UInt64 { (self[k] as! NSNumber).uint64Value }
}

func i64(_ x: Any) -> Int64 { (x as! NSNumber).int64Value }
func u32(_ x: Any) -> UInt32 { (x as! NSNumber).uint32Value }
func pair(_ x: Any) -> Rational { let a = x as! [Any]; return try! Rational(i64(a[0]), i64(a[1])) }
func ints(_ x: Any) -> [Int32] { (x as! [Any]).map { Int32(truncatingIfNeeded: i64($0)) } }
func hex(_ s: String) -> [UInt8] { bytes(hex: s)! }
