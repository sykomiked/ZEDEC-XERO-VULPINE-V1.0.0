// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The one ZXV look, the same as zui, the bare-metal desktop and the boot:
// the #002911 green ground with carved steps of the same hue, light green
// text and one gold #F2C230 accent. Every value comes from ZUIToken
// (ThemeTokens.swift, generated from kernel/src/zui/zui_tokens.h), so
// Studio cannot drift from the native screens. Text is ASCW Golden
// (ASCWFont.swift); numbers and IDs use Theme.mono.
import CoreText
import SwiftUI
import ZXVRPC

enum Theme {
    static let ground = ZUIToken.ground
    static let sidebar = ZUIToken.panel
    static let panel = ZUIToken.panel
    static let inset = ZUIToken.raised
    static let border = ZUIToken.edge
    static let text = ZUIToken.text
    static let muted = ZUIToken.muted
    static let gold = ZUIToken.gold
    static let good = ZUIToken.ok
    static let warn = ZUIToken.alert
    static let floor = ZUIToken.info
    static let emotional = ZUIToken.goldGlow
    static let radiusPanel = ZUIToken.radiusPanel
    static let radiusControl = ZUIToken.radiusControl
}

extension Theme {
    /// ASCW Golden at a size; the macOS text-style sizes below.
    static func ui(_ size: CGFloat) -> Font { ASCWFont.font(size: size) }
    static var body: Font { ui(ZUIToken.body) }
    static var callout: Font { ui(ZUIToken.label) }
    static var caption: Font { ui(ZUIToken.caption) }
    static var caption2: Font { ui(ZUIToken.caption) }

    /// The family's monospaced face for numbers, IDs, paths and code: ASCW
    /// Golden Mono once assets/fonts/ascw-golden carries it, else the
    /// system's monospaced face.
    static func mono(_ size: CGFloat) -> Font {
        let ct = CTFontCreateWithName(monoFamily as CFString, size, nil)
        if (CTFontCopyFamilyName(ct) as String) == monoFamily { return Font(ct) }
        return .system(size: size, design: .monospaced)
    }

    static let monoFamily = "ASCW Golden Mono"
}

@MainActor
struct Panel<Content: View>: View {
    let title: String
    var trailing: String? = nil
    var dashed = false
    @ViewBuilder let content: Content

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Text(title.uppercased())
                    .font(Theme.ui(12))
                    .kerning(1)
                    .foregroundStyle(Theme.muted)
                Spacer()
                if let t = trailing {
                    Text(t).font(Theme.caption).foregroundStyle(Theme.muted)
                }
            }
            content
        }
        .padding(18)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(dashed ? Color.clear : Theme.panel, in: RoundedRectangle(cornerRadius: Theme.radiusPanel))
        .overlay(
            RoundedRectangle(cornerRadius: Theme.radiusPanel)
                .strokeBorder(dashed ? Theme.muted.opacity(0.5) : Theme.border,
                              style: StrokeStyle(lineWidth: 1, dash: dashed ? [5, 4] : []))
        )
    }
}

/// A label and a monospaced value on one row.
@MainActor
struct KV: View {
    let key: String
    let value: String
    var color: Color = Theme.text

    var body: some View {
        HStack {
            Text(key).foregroundStyle(Theme.muted)
            Spacer()
            Text(value).font(Theme.mono(ZUIToken.body)).foregroundStyle(color)
                .textSelection(.enabled)
        }
        .font(Theme.callout)
    }
}

/// A big number with a caption, for counters.
@MainActor
struct Stat: View {
    let value: String
    let label: String

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(value).font(Theme.mono(20))
            Text(label).font(Theme.caption).foregroundStyle(Theme.muted)
        }
        .padding(12)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Theme.inset, in: RoundedRectangle(cornerRadius: Theme.radiusControl))
        .overlay(RoundedRectangle(cornerRadius: Theme.radiusControl).strokeBorder(Theme.border))
    }
}

/// Shown where a mockup panel needs a zxvd method that is not served yet.
@MainActor
struct NotServed: View {
    let what: String
    let method: String
    let detail: String

    var body: some View {
        Panel(title: what, dashed: true) {
            Text("Not served by zxvd yet")
                .font(Theme.ui(15))
            Text(detail).foregroundStyle(Theme.text.opacity(0.85))
            Text("Appears when zxvd lists \(method) in rpc.discover.")
                .font(Theme.caption).foregroundStyle(Theme.muted)
        }
    }
}

@MainActor
struct PageHeader<Trailing: View>: View {
    let title: String
    let subtitle: String
    @ViewBuilder var trailing: Trailing

    var body: some View {
        HStack(alignment: .center, spacing: 16) {
            VStack(alignment: .leading, spacing: 4) {
                Text(title).font(Theme.ui(26))
                Text(subtitle).foregroundStyle(Theme.muted)
            }
            Spacer()
            trailing
        }
    }
}

extension PageHeader where Trailing == EmptyView {
    init(title: String, subtitle: String) {
        self.init(title: title, subtitle: subtitle) { EmptyView() }
    }
}

func grouped(_ v: Int64) -> String { Money.groupThousands(String(v.magnitude)).withSign(v < 0) }
func grouped(_ v: Int) -> String { grouped(Int64(v)) }

private extension String {
    func withSign(_ neg: Bool) -> String { neg ? "\u{2212}" + self : self }
}
