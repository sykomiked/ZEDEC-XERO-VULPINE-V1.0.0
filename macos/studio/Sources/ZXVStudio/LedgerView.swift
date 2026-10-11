// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Sicily Ledger & Patron Deck: ledger.accounts / balance / entries / post /
// open_account / check, and swarm.state.quest for the deck.
import SwiftUI
import ZXVRPC

@MainActor
struct LedgerView: View {
    @EnvironmentObject var store: AppStore
    @State private var showOpen = false
    @State private var newName = ""
    @State private var newCurrency = "USD"
    @State private var newKind = "holder"
    @State private var openLine: (Bool, String)?
    @State private var to: Int?
    @State private var amount = ""
    @State private var reference = ""
    @State private var postLine: (Bool, String)?
    @State private var posting = false

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            PageHeader(title: "Sicily Ledger & Patron Deck",
                       subtitle: "Your books in whole minor units. Every transfer balances; fractions are refused, never rounded.") {
                HStack {
                    Button {
                        Task { await store.checkBooks() }
                    } label: { Label("Check books", systemImage: "checkmark") }
                    Button("Open account") { showOpen.toggle() }
                }
                .controlSize(.large)
            }
            if showOpen { openAccount }
            if store.accounts.isEmpty {
                Panel(title: "Accounts") {
                    Text("No accounts yet. Enable a currency in Settings, then open an issuer and a holder account here.")
                        .foregroundStyle(Theme.muted)
                }
            } else {
                HStack(alignment: .top, spacing: 20) {
                    accountList.frame(width: 280)
                    VStack(spacing: 16) {
                        rails
                        journal
                        transfer
                    }
                }
            }
            deck
        }
        .task { await store.refreshLedger() }
    }

    private var openAccount: some View {
        Panel(title: "Open account") {
            HStack(alignment: .bottom, spacing: 12) {
                VStack(alignment: .leading) {
                    Text("Name").font(.caption).foregroundStyle(Theme.muted)
                    TextField("Travel fund", text: $newName).textFieldStyle(.roundedBorder)
                }
                VStack(alignment: .leading) {
                    Text("Currency").font(.caption).foregroundStyle(Theme.muted)
                    Picker("Currency", selection: $newCurrency) {
                        ForEach(currencies, id: \.self) { Text($0).tag($0) }
                    }
                    .labelsHidden().frame(width: 110)
                }
                VStack(alignment: .leading) {
                    Text("Kind").font(.caption).foregroundStyle(Theme.muted)
                    Picker("Kind", selection: $newKind) {
                        Text("holder").tag("holder")
                        Text("issuer").tag("issuer")
                    }
                    .labelsHidden().frame(width: 110)
                }
                Button("Open") {
                    Task {
                        openLine = await store.openAccount(name: newName, currency: newCurrency, kind: newKind)
                        if openLine?.0 == true {
                            newName = ""
                            showOpen = false
                        }
                    }
                }
                .buttonStyle(.borderedProminent)
            }
            if let l = openLine { Text(l.1).font(.callout).foregroundStyle(l.0 ? Theme.muted : Theme.warn) }
        }
    }

    private var currencies: [String] {
        store.currenciesEnabled.isEmpty ? ["USD"] : store.currenciesEnabled
    }

    private var accountList: some View {
        Panel(title: "Accounts") {
            VStack(spacing: 4) {
                ForEach(store.accounts) { a in
                    Button {
                        Task { await store.select(a.account) }
                        to = nil
                        postLine = nil
                    } label: {
                        HStack {
                            VStack(alignment: .leading, spacing: 2) {
                                Text(a.name)
                                Text("\(a.kind) · \(a.currency)").font(.caption).foregroundStyle(Theme.muted)
                            }
                            Spacer()
                        }
                        .padding(.horizontal, 12)
                        .padding(.vertical, 9)
                        .contentShape(Rectangle())
                        .background(a.account == store.selectedAccount ? Theme.border : Color.clear,
                                    in: RoundedRectangle(cornerRadius: 10))
                    }
                    .buttonStyle(.plain)
                }
            }
        }
    }

    private var rails: some View {
        let ccy = store.selected?.currency ?? ""
        let b = store.balance
        return HStack(spacing: 14) {
            RailTile(title: "Assets · Neo-Roman", rail: "debit rail 555", minor: b?.debit, raw: b?.debitMinor, currency: ccy)
            RailTile(title: "Liabilities · New California", rail: "credit rail 777", minor: b?.credit, raw: b?.creditMinor, currency: ccy)
            RailTile(title: "Equity · Sicilian Crown", rail: "equity rail 888", minor: b?.equity, raw: b?.equityMinor, currency: ccy)
        }
    }

    private var journal: some View {
        let ccy = store.selected?.currency ?? ""
        return Panel(title: "Journal · \(store.selected?.name ?? "")") {
            if store.entries.isEmpty {
                Text("No entries yet.").foregroundStyle(Theme.muted)
            } else {
                Grid(alignment: .leading, horizontalSpacing: 16, verticalSpacing: 8) {
                    GridRow {
                        Text("Entry"); Text("Reference"); Text("Counterparty")
                        Text("Amount").gridColumnAlignment(.trailing)
                    }
                    .font(.caption).foregroundStyle(Theme.muted)
                    ForEach(store.entries.reversed()) { e in
                        GridRow {
                            Text(e.entryId).font(.system(.callout, design: .monospaced)).foregroundStyle(Theme.muted)
                            Text(e.reference)
                            Text(store.name(of: e.counterparty))
                            Text(e.signed.map { ($0 > 0 ? "+" : "") + Money.format($0, currency: ccy) } ?? e.signedMinor)
                                .font(.system(.callout, design: .monospaced))
                                .foregroundStyle((e.signed ?? 0) < 0 ? Theme.warn : Theme.good)
                        }
                        .font(.callout)
                    }
                }
            }
        }
    }

    private var transfer: some View {
        let from = store.selected
        let targets = store.accounts.filter { $0.account != from?.account && $0.currency == from?.currency }
        return Panel(title: "New transfer") {
            HStack(alignment: .bottom, spacing: 12) {
                VStack(alignment: .leading) {
                    Text("From").font(.caption).foregroundStyle(Theme.muted)
                    Text(from?.name ?? "").padding(.vertical, 4)
                }
                VStack(alignment: .leading) {
                    Text("To").font(.caption).foregroundStyle(Theme.muted)
                    Picker("To", selection: Binding(get: { to ?? targets.first?.account }, set: { to = $0 })) {
                        ForEach(targets) { Text($0.name).tag(Optional($0.account)) }
                    }
                    .labelsHidden().frame(minWidth: 150)
                }
                VStack(alignment: .leading) {
                    Text("Amount (\(from?.currency ?? ""))").font(.caption).foregroundStyle(Theme.muted)
                    TextField("125.00", text: $amount).textFieldStyle(.roundedBorder).frame(width: 140)
                }
                VStack(alignment: .leading) {
                    Text("Reference").font(.caption).foregroundStyle(Theme.muted)
                    TextField("Invoice 0042", text: $reference).textFieldStyle(.roundedBorder)
                }
                Button("Post transfer") {
                    guard let dest = to ?? targets.first?.account else {
                        postLine = (false, "Open a second \(from?.currency ?? "") account to transfer to.")
                        return
                    }
                    posting = true
                    Task {
                        postLine = await store.post(to: dest, amount: amount, reference: reference)
                        if postLine?.0 == true {
                            amount = ""
                            reference = ""
                        }
                        posting = false
                    }
                }
                .buttonStyle(.borderedProminent)
                .disabled(posting || from == nil)
            }
            Text(postLine?.1 ?? "Amounts go to zxvd as whole minor units.")
                .font(.callout)
                .foregroundStyle(postLine?.0 == false ? Theme.warn : Theme.muted)
        }
    }

    private var deck: some View {
        let q = store.state?.quest
        return Panel(title: "Patron Deck",
                     trailing: "Contributed \(grouped(q?.contributed ?? 0)) units · levels and points only, no ranks or streaks") {
            HStack(alignment: .top, spacing: 20) {
                if store.has("player.card") {
                    Panel(title: "Player Card") { Text("Served by zxvd; shown here once its shape is agreed.") }
                        .frame(width: 280)
                } else {
                    NotServed(what: "Player Card", method: "player.card",
                              detail: "Tiers run Copper/Obsidian to Curzi/Admas and are earned from credibility, bond and uptime.")
                        .frame(width: 280)
                }
                LazyVGrid(columns: [GridItem(.adaptive(minimum: 180), spacing: 12)], spacing: 12) {
                    ForEach(q?.skills ?? []) { SkillCard(skill: $0) }
                }
            }
        }
    }
}

@MainActor
struct RailTile: View {
    let title: String
    let rail: String
    let minor: Int64?
    let raw: String?
    let currency: String

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(title).font(.caption).foregroundStyle(Theme.muted)
            Text(minor.map { Money.format($0, currency: currency) } ?? "\u{2014}")
                .font(.system(size: 22, design: .monospaced))
                .lineLimit(1).minimumScaleFactor(0.6)
            Text("\(rail) · \(raw ?? "?") minor").font(.system(size: 11, design: .monospaced)).foregroundStyle(Theme.muted)
        }
        .padding(16)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Theme.panel, in: RoundedRectangle(cornerRadius: 14))
        .overlay(RoundedRectangle(cornerRadius: 14).strokeBorder(Theme.border))
    }
}

@MainActor
struct SkillCard: View {
    let skill: Skill
    /// Points needed to reach each level (kernel/src/quest/quest.c thresholds).
    static let thresholds = [0, 8, 21, 55, 144, 377, 987, 2584]

    var body: some View {
        let lv = min(max(skill.level, 0), Self.thresholds.count - 1)
        let floor = Self.thresholds[lv]
        let top = lv + 1 < Self.thresholds.count ? Self.thresholds[lv + 1] : floor
        let frac = top == floor ? 1 : Double(skill.points - floor) / Double(top - floor)
        return VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text(skill.skill.capitalized).fontWeight(.medium)
                Spacer()
                Text("Level \(skill.level)").font(.caption).foregroundStyle(Theme.gold)
            }
            ProgressView(value: min(max(frac, 0), 1)).tint(Theme.gold)
            Text(top == floor ? "\(skill.points) pts · top level" : "\(skill.points) pts · \(top - skill.points) to level \(lv + 1)")
                .font(.system(size: 11, design: .monospaced)).foregroundStyle(Theme.muted)
        }
        .padding(14)
        .background(Theme.inset, in: RoundedRectangle(cornerRadius: 12))
        .overlay(RoundedRectangle(cornerRadius: 12).strokeBorder(Theme.border))
    }
}
