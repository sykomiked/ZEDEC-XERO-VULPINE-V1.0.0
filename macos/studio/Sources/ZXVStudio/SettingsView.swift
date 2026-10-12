// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Settings & Jurisdiction Bridge: zxvd.version, update.check and
// swarm.state.update, notes.list / notes.mark_read, currency.enable,
// swarm.state's machine budget, jurisdiction.get / jurisdiction.set,
// zxvd.shutdown.
import SwiftUI
import ZXVRPC

@MainActor
struct SettingsView: View {
    @EnvironmentObject var store: AppStore
    @State private var updateLine: String?
    @State private var checking = false
    @State private var code = ""
    @State private var codeLine: (Bool, String)?
    @State private var confirmStop = false
    @State private var market = "closed"
    @State private var standard = ""
    @State private var country = ""
    @State private var bridgeLine: (Bool, String)?
    @State private var bridgeLoaded = false

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            PageHeader(title: "Settings & Jurisdiction Bridge",
                       subtitle: "Updates, notifications, currencies, this Mac's budget, and the rules your books follow.")
            HStack(alignment: .top, spacing: 20) {
                updates
                notifications
            }
            HStack(alignment: .top, spacing: 20) {
                currencies
                machine
            }
            if store.has("jurisdiction.get") {
                bridge
            } else {
                NotServed(what: "Jurisdiction Bridge", method: "jurisdiction.get",
                          detail: "Closed clearing is the default; an open batch-auction market is a per-deployment switch. The accounting standard for reports (IFRS, US GAAP, AAOIFI, OHADA, CAS, insurer SAP) will be chosen here. Adapters are designed to follow each standard; they are not a substitute for an accountant or a Shariah board.")
            }
        }
    }

    private static let standardNames: [String: String] = [
        "": "Not chosen", "IFRS": "IFRS", "US_GAAP": "US GAAP", "IPSAS": "IPSAS", "AAOIFI": "AAOIFI",
        "OHADA": "OHADA", "CAS": "CAS (China)", "SAP_INSURER": "Insurer SAP",
    ]

    private var bridge: some View {
        let j = store.jurisdiction
        return Panel(title: "Jurisdiction Bridge", trailing: j?.inEffect == true ? "In effect" : "Recorded only") {
            Picker("Market", selection: $market) {
                Text("Closed clearing").tag("closed")
                Text("Open batch auction").tag("open")
            }
            .pickerStyle(.segmented).frame(width: 340)
            HStack {
                Picker("Accounting standard", selection: $standard) {
                    ForEach([""] + (j?.standards ?? []), id: \.self) { Text(Self.standardNames[$0] ?? $0).tag($0) }
                }
                .frame(width: 300)
                TextField("Country (e.g. KE)", text: $country).textFieldStyle(.roundedBorder).frame(width: 140)
                Button("Save") {
                    Task { bridgeLine = await store.setJurisdiction(market: market, standard: standard, country: country) }
                }
                .buttonStyle(.borderedProminent)
            }
            Text(bridgeLine?.1 ?? "Saved in your books file. No report adapter or open market uses these settings yet. Adapters are designed to follow each standard; they are not a substitute for an accountant or a Shariah board.")
                .font(.callout)
                .foregroundStyle(bridgeLine?.0 == false ? Theme.warn : Theme.muted)
        }
        .onAppear { loadBridge() }
        .onChange(of: store.jurisdiction?.market) { _ in loadBridge() }
    }

    private func loadBridge() {
        guard !bridgeLoaded, let j = store.jurisdiction else { return }
        market = j.market
        standard = j.standard
        country = j.country
        bridgeLoaded = true
    }

    private var updates: some View {
        let u = store.state?.update
        return Panel(title: "Updates") {
            KV(key: "App", value: store.version?.app ?? "\u{2014}")
            KV(key: "Protocol", value: store.version?.protocol ?? "\u{2014}")
            KV(key: "libzxv ABI", value: store.version?.libzxv ?? "\u{2014}")
            KV(key: "Automatic checks", value: u?.auto == true ? "on while online" : "off")
            KV(key: "Checks run", value: grouped(u?.checks ?? 0))
            Text(updateLine ?? (u?.status.isEmpty == false ? u!.status : "No check has run yet."))
                .font(.callout)
                .padding(10)
                .frame(maxWidth: .infinity, alignment: .leading)
                .background(Theme.inset, in: RoundedRectangle(cornerRadius: 8))
            if let e = u?.error, !e.isEmpty { Text(e).font(.caption).foregroundStyle(Theme.warn) }
            HStack {
                Text("Updates are signed. While the network is off, only Check now contacts the gateway.")
                    .font(.caption).foregroundStyle(Theme.muted)
                Spacer()
                Button(checking ? "Checking" : "Check now") {
                    checking = true
                    Task {
                        updateLine = await store.checkUpdate()
                        checking = false
                    }
                }
                .disabled(checking)
            }
        }
    }

    private var notifications: some View {
        Panel(title: "Notifications · \(store.unread) unread") {
            HStack {
                Spacer()
                Button("Mark all read") { Task { await store.markAllRead() } }
                    .disabled(store.unread == 0)
            }
            if store.notes.isEmpty {
                Text("No notifications.").foregroundStyle(Theme.muted)
            }
            ForEach(store.notes) { n in
                HStack(alignment: .top, spacing: 10) {
                    Circle().fill(n.read ? Theme.border : Theme.gold).frame(width: 8, height: 8).padding(.top, 6)
                    VStack(alignment: .leading, spacing: 2) {
                        Text(n.count > 1 ? "\(n.title) (\(n.count))" : n.title).fontWeight(.medium)
                        if !n.body.isEmpty { Text(n.body).font(.callout) }
                        Text("\(n.kind) · from \(n.source)").font(.caption).foregroundStyle(Theme.muted)
                    }
                    Spacer()
                }
                .padding(10)
                .background(Theme.inset, in: RoundedRectangle(cornerRadius: 8))
            }
        }
    }

    private var currencies: some View {
        Panel(title: "Currencies in your books") {
            if store.currenciesEnabled.isEmpty {
                Text("None enabled yet.").foregroundStyle(Theme.muted)
            } else {
                HStack {
                    ForEach(store.currenciesEnabled, id: \.self) { c in
                        Text(c).font(.system(.body, design: .monospaced))
                            .padding(.horizontal, 12).padding(.vertical, 6)
                            .overlay(Capsule().strokeBorder(Theme.border))
                    }
                }
            }
            HStack {
                TextField("ISO 4217 code, like GBP", text: $code).textFieldStyle(.roundedBorder).frame(width: 220)
                Button("Enable") {
                    Task {
                        codeLine = await store.enableCurrency(code)
                        if codeLine?.0 == true { code = "" }
                    }
                }
                .buttonStyle(.borderedProminent)
            }
            Text(codeLine?.1 ?? "Enabled currencies can hold accounts in the Sicily Ledger. This list shows the ones in use; zxvd has no currency list method yet.")
                .font(.callout)
                .foregroundStyle(codeLine?.0 == false ? Theme.warn : Theme.muted)
        }
    }

    private var machine: some View {
        let s = store.state
        return Panel(title: "This Mac's budget") {
            KV(key: "Cores", value: s.map { "\($0.cores) total, \(String(format: "%.2f", Double($0.coresMilli) / 1000)) granted" } ?? "\u{2014}")
            KV(key: "Memory", value: s.map { "\(grouped($0.memTotalMb)) MB, \(grouped($0.memMb)) granted" } ?? "\u{2014}")
            KV(key: "Swarm levels", value: s.map { String($0.levels) } ?? "\u{2014}")
            KV(key: "Tokens per cycle", value: grouped(s?.tokensPerCycle ?? 0))
            Divider()
            HStack {
                Text(confirmStop ? "Stop the daemon? The swarm and the network stop with it." : "The swarm runs in the zxvd daemon.")
                    .font(.callout)
                Spacer()
                if confirmStop {
                    Button("Cancel") { confirmStop = false }
                    Button("Stop zxvd", role: .destructive) {
                        confirmStop = false
                        Task { await store.shutdownDaemon() }
                    }
                } else {
                    Button("Stop daemon") { confirmStop = true }
                }
            }
        }
    }
}
