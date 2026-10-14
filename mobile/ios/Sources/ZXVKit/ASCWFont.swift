// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// ASCW Golden, the UI font (assets/fonts/ascw-golden; SIL OFL 1.1). The app
// registers the faces for its own process at launch, then every text style
// is a CoreText font whose cascade list is the rest of the family: the face
// for the user's language first (Golden Persian for fa, Golden Japanese for
// ja, ...), then ASCW Golden Latin II, then every other Golden script face,
// then the system's fonts. The same file is in macos/studio (ZXV Studio).
//
// One weight: the family has Regular only, so a .semibold request draws
// Regular (CoreText does not invent a bold). Numbers and IDs stay in the
// system monospaced face; ASCW has no monospaced member.
import CoreText
import Foundation
import SwiftUI

enum ASCWFont {
    /// Whether the faces were found and registered; false means system fonts.
    private(set) static var ready = false

    /// Register every face in `dir` (default: the bundle's Fonts folder, else
    /// $ZXV_FONTS_DIR) for this process. Safe to call more than once.
    @discardableResult
    static func registerAll(from dir: URL? = nil) -> Bool {
        if ready { return true }
        var cands: [URL] = []
        if let d = dir { cands.append(d) }
        if let r = Bundle.main.resourceURL { cands.append(r.appendingPathComponent("Fonts")) }
        if let e = ProcessInfo.processInfo.environment["ZXV_FONTS_DIR"] { cands.append(URL(fileURLWithPath: e)) }
        for d in cands {
            let urls = ASCWGoldenMap.files.map { d.appendingPathComponent($0) }
            guard FileManager.default.fileExists(atPath: urls[0].path) else { continue }
            let present = urls.filter { FileManager.default.fileExists(atPath: $0.path) }
            CTFontManagerRegisterFontURLs(present as CFArray, .process, true, nil)
            ready = true
            return true
        }
        return false
    }

    /// The family for a locale, as kernel/src/font/ascw_golden.c picks it.
    static func family(for locale: Locale = .current) -> String {
        var tag = locale.identifier.replacingOccurrences(of: "_", with: "-")
        while !tag.isEmpty {
            if let f = ASCWGoldenMap.locales.first(where: { $0.key.caseInsensitiveCompare(tag) == .orderedSame }) {
                return f.value
            }
            tag = tag.contains("-") ? String(tag[..<tag.lastIndex(of: "-")!]) : ""
        }
        if let s = locale.language.script?.identifier, let f = ASCWGoldenMap.scripts[s] { return f }
        // CLDR's likely script when the locale names none
        switch locale.language.languageCode?.identifier ?? "" {
        case "ja": return ASCWGoldenMap.scripts["Jpan"] ?? ASCWGoldenMap.primary
        case "ko": return ASCWGoldenMap.scripts["Kore"] ?? ASCWGoldenMap.primary
        case "zh":
            let r = locale.region?.identifier ?? ""
            return ASCWGoldenMap.scripts[["TW", "HK", "MO"].contains(r) ? "Hant" : "Hans"] ?? ASCWGoldenMap.primary
        default: return ASCWGoldenMap.primary
        }
    }

    private static var cache: [CGFloat: Font] = [:]

    /// ASCW Golden at `size` points, with the whole family behind it.
    static func font(size: CGFloat) -> Font {
        if let f = cache[size] { return f }
        guard ready else { return .system(size: size) }
        let first = family(for: .current)
        var names = [first]
        if first != ASCWGoldenMap.primary { names.append(ASCWGoldenMap.primary) }
        names += ASCWGoldenMap.cascade.filter { !names.contains($0) }
        let cascade = names.dropFirst().map {
            CTFontDescriptorCreateWithAttributes([kCTFontFamilyNameAttribute: $0] as CFDictionary)
        }
        let desc = CTFontDescriptorCreateWithAttributes([
            kCTFontFamilyNameAttribute: first,
            kCTFontCascadeListAttribute: cascade,
        ] as CFDictionary)
        let f = Font(CTFontCreateWithFontDescriptor(desc, size, nil))
        cache[size] = f
        return f
    }
}
