// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import CoreImage.CIFilterBuiltins
import SwiftUI

/// Colours of the desktop mockups, as in the Android ui/Theme.kt.
enum Zc {
    static let bg = Color(red: 0x0E / 255, green: 0x11 / 255, blue: 0x16 / 255)
    static let surface = Color(red: 0x16 / 255, green: 0x1B / 255, blue: 0x22 / 255)
    static let raised = Color(red: 0x1F / 255, green: 0x26 / 255, blue: 0x30 / 255)
    static let text = Color(red: 0xE6 / 255, green: 0xED / 255, blue: 0xF3 / 255)
    static let muted = Color(red: 0x9A / 255, green: 0xA4 / 255, blue: 0xAE / 255)
    static let gold = Color(red: 0xD4 / 255, green: 0xA7 / 255, blue: 0x2C / 255)
    static let green = Color(red: 0x3F / 255, green: 0xB9 / 255, blue: 0x50 / 255)
}

public struct RootView: View {
    @ObservedObject var m: AppModel
    public init(model: AppModel) { m = model }

    public var body: some View {
        TabView(selection: $m.tab) {
            AssistantView(m: m).tabItem { Label(Tab.assistant.rawValue, systemImage: Tab.assistant.icon) }.tag(Tab.assistant)
            Placeholder(title: "Feed and groups", note: "Posts from your groups appear here once the feed is bridged.")
                .tabItem { Label(Tab.feed.rawValue, systemImage: Tab.feed.icon) }.tag(Tab.feed)
            Placeholder(title: "Calls", note: "Encrypted peer-to-peer calls; the call engine is not bridged yet.")
                .tabItem { Label(Tab.calls.rawValue, systemImage: Tab.calls.icon) }.tag(Tab.calls)
            WalletView(m: m).tabItem { Label(Tab.wallet.rawValue, systemImage: Tab.wallet.icon) }.tag(Tab.wallet)
            NetworkView(m: m).tabItem { Label(Tab.network.rawValue, systemImage: Tab.network.icon) }.tag(Tab.network)
        }
        .tint(Zc.gold)
        .preferredColorScheme(.dark)
        .sheet(item: $m.sas) { s in
            VStack(spacing: 16) {
                Text("Do both screens show this number?").font(.headline)
                Text(s.digits).font(.system(size: 40, weight: .semibold, design: .monospaced)).foregroundColor(Zc.gold)
                HStack {
                    Button("No") { m.answerSas(false) }.buttonStyle(.bordered)
                    Button("Yes, they match") { m.answerSas(true) }.buttonStyle(.borderedProminent)
                }
            }.padding()
        }
        .alert(item: $m.money) { p in
            Alert(title: Text("Approve \(Vfv.format(Int64(p.amountMinor))) VFV?"),
                  message: Text("\(Vfv.railName(p.rail)) — \(p.memo)"),
                  primaryButton: .default(Text("Approve")) { m.answerMoney(true) },
                  secondaryButton: .cancel(Text("Decline")) { m.answerMoney(false) })
        }
    }
}

struct AssistantView: View {
    @ObservedObject var m: AppModel
    @State private var draft = ""

    var body: some View {
        VStack(spacing: 0) {
            Text(m.status).font(.caption).foregroundColor(Zc.muted).frame(maxWidth: .infinity).padding(6).background(Zc.surface)
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 8) {
                    ForEach(m.messages) { msg in
                        VStack(alignment: msg.fromUser ? .trailing : .leading, spacing: 2) {
                            Text(msg.text).padding(10).background(msg.fromUser ? Zc.raised : Zc.surface).cornerRadius(10)
                            if !msg.place.isEmpty { Text(msg.place).font(.caption2).foregroundColor(Zc.muted) }
                        }.frame(maxWidth: .infinity, alignment: msg.fromUser ? .trailing : .leading)
                    }
                }.padding()
            }
            HStack {
                TextField("Ask anything", text: $draft).textFieldStyle(.roundedBorder)
                Button("Send") { m.send(draft); draft = "" }.disabled(draft.isEmpty)
            }.padding()
        }.background(Zc.bg)
    }
}

struct WalletView: View {
    @ObservedObject var m: AppModel
    @State private var amount = 10_000.0

    var body: some View {
        Form {
            Section("Rails") {
                Text(Vfv.railName(555)); Text(Vfv.railName(777)); Text(Vfv.railName(888))
            }
            Section("Tithe preview") {
                Slider(value: $amount, in: 0...1_000_000, step: 100)
                Text("\(Vfv.format(Int64(amount))) VFV → tithe \(Vfv.format(Int64(m.tithe(UInt64(amount))))) VFV")
                    .font(.system(.body, design: .monospaced))
            }
            Section { Text("Payments need a confirmation on a device you hold. No interest is ever charged.").foregroundColor(Zc.muted) }
        }
    }
}

struct NetworkView: View {
    @ObservedObject var m: AppModel
    @State private var code = ""

    var body: some View {
        Form {
            Section("Devices") {
                ForEach(m.devices) { d in
                    HStack {
                        Text(d.name.isEmpty ? "device" : d.name)
                        Spacer()
                        Text(d.revoked ? "revoked" : d.role == 1 ? "home" : "phone").foregroundColor(d.revoked ? .red : Zc.muted)
                    }.swipeActions { if !d.revoked { Button("Revoke", role: .destructive) { m.revoke(d) } } }
                }
            }
            Section("Pair a device") {
                Button("Show pairing code") { m.invite() }
                if let inv = m.invitation {
                    if let img = qr(inv.qr) { Image(uiImage: img).interpolation(.none).resizable().scaledToFit().frame(height: 220) }
                    Text(inv.code).font(.system(.title2, design: .monospaced)).foregroundColor(Zc.gold)
                }
                TextField("Enter a 10-character code", text: $code).textInputAutocapitalization(.characters)
                Button("Join") { m.join(code: code); code = "" }.disabled(code.count != 10)
            }
            Section("Models on this phone") {
                if m.models.isEmpty { Text("None fit; prompts go to a paired computer.").foregroundColor(Zc.muted) }
                ForEach(m.models, id: \.self) { Text($0).font(.system(.body, design: .monospaced)) }
            }
            Section("Buy capacity") {
                Text("When no device of yours can run a request, capacity can be bought from other people at one uniform price per round, paid only on delivery.")
                    .foregroundColor(Zc.muted)
            }
        }
    }

    private func qr(_ text: String) -> UIImage? {
        let f = CIFilter.qrCodeGenerator()
        f.message = Data(text.utf8)
        f.correctionLevel = "M"
        guard let out = f.outputImage?.transformed(by: CGAffineTransform(scaleX: 8, y: 8)),
              let cg = CIContext().createCGImage(out, from: out.extent) else { return nil }
        return UIImage(cgImage: cg)
    }
}

struct Placeholder: View {
    let title: String
    let note: String
    var body: some View {
        VStack(spacing: 8) {
            Text(title).font(.title3).foregroundColor(Zc.text)
            Text(note).foregroundColor(Zc.muted).multilineTextAlignment(.center)
        }.padding().frame(maxWidth: .infinity, maxHeight: .infinity).background(Zc.bg)
    }
}
