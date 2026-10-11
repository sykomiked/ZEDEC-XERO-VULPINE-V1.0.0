// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The look approved in the mockups: dark ground, gold accent, IBM Plex-like
// system faces (SF and SF Mono), panels with a thin border.
import SwiftUI
import ZXVRPC

enum Theme {
    static let ground = Color(red: 0x0C / 255, green: 0x0F / 255, blue: 0x14 / 255)
    static let sidebar = Color(red: 0x10 / 255, green: 0x14 / 255, blue: 0x1B / 255)
    static let panel = Color(red: 0x14 / 255, green: 0x19 / 255, blue: 0x22 / 255)
    static let inset = Color(red: 0x0F / 255, green: 0x13 / 255, blue: 0x19 / 255)
    static let border = Color(red: 0x22 / 255, green: 0x2A / 255, blue: 0x35 / 255)
    static let text = Color(red: 0xE7 / 255, green: 0xE9 / 255, blue: 0xEE / 255)
    static let muted = Color(red: 0x9A / 255, green: 0xA3 / 255, blue: 0xB2 / 255)
    static let gold = Color(red: 0xD9 / 255, green: 0xA9 / 255, blue: 0x2E / 255)
    static let good = Color(red: 0x8F / 255, green: 0xD3 / 255, blue: 0xA8 / 255)
    static let warn = Color(red: 0xF0 / 255, green: 0xA3 / 255, blue: 0x5E / 255)
    static let floor = Color(red: 0x4C / 255, green: 0x8D / 255, blue: 0xFF / 255)
    static let emotional = Color(red: 0xE0 / 255, green: 0x5F / 255, blue: 0xA8 / 255)
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
                    .font(.system(size: 12, weight: .semibold))
                    .kerning(1)
                    .foregroundStyle(Theme.muted)
                Spacer()
                if let t = trailing {
                    Text(t).font(.caption).foregroundStyle(Theme.muted)
                }
            }
            content
        }
        .padding(18)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(dashed ? Color.clear : Theme.panel, in: RoundedRectangle(cornerRadius: 14))
        .overlay(
            RoundedRectangle(cornerRadius: 14)
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
            Text(value).font(.system(.body, design: .monospaced)).foregroundStyle(color)
                .textSelection(.enabled)
        }
        .font(.callout)
    }
}

/// A big number with a caption, for counters.
@MainActor
struct Stat: View {
    let value: String
    let label: String

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(value).font(.system(size: 20, design: .monospaced))
            Text(label).font(.caption).foregroundStyle(Theme.muted)
        }
        .padding(12)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Theme.inset, in: RoundedRectangle(cornerRadius: 10))
        .overlay(RoundedRectangle(cornerRadius: 10).strokeBorder(Theme.border))
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
                .font(.title3.weight(.semibold))
            Text(detail).foregroundStyle(Theme.text.opacity(0.85))
            Text("Appears when zxvd lists \(method) in rpc.discover.")
                .font(.caption).foregroundStyle(Theme.muted)
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
                Text(title).font(.system(size: 26, weight: .semibold))
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
