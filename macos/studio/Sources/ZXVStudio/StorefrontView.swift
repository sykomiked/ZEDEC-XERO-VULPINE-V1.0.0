// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Storefront & Escrow: net.command sell / want, net.offers, swarm.state.net's
// market counters and swarm.state's settlement spine.
import SwiftUI
import ZXVRPC

@MainActor
struct StorefrontView: View {
    @EnvironmentObject var store: AppStore
    @State private var selling = true
    @State private var resource = NetResource.compute
    @State private var units = ""
    @State private var price = ""
    @State private var node = ""
    @State private var posted: [String] = []

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            PageHeader(title: "Storefront & Escrow",
                       subtitle: "Sell spare compute and storage to peers, or post what you need. Every trade settles through your ledger with the 0.08889% assurance fee.")
            HStack(alignment: .top, spacing: 20) {
                offer
                market.frame(width: 320)
            }
            HStack(alignment: .top, spacing: 20) {
                settlement
                VStack(spacing: 16) {
                    if !store.has("ledger.holds") {
                        NotServed(what: "Escrow", method: "ledger.holds",
                                  detail: "Each hold will show the amount held, the counterparty, what releases it, and whether it is held, released or in dispute. A dispute moves the hold to quarantine.")
                    }
                    if store.has("net.offers") {
                        openOffers
                    } else {
                        NotServed(what: "Open offers", method: "net.offers",
                                  detail: "Your open sell offers and wants. zxvd reports only the number of wants today.")
                    }
                }
                .frame(width: 320)
            }
        }
    }

    private var command: NetCommand {
        let u = UInt64(units.trimmingCharacters(in: .whitespaces)) ?? 0
        let p = UInt64(price.trimmingCharacters(in: .whitespaces)) ?? 0
        return selling
            ? .sell(resource, units: u, price: p, nodeID: node.trimmingCharacters(in: .whitespaces))
            : .want(resource, units: u, maxPrice: p)
    }

    private var offer: some View {
        Panel(title: "Post an offer") {
            Picker("Offer", selection: $selling) {
                Text("Sell").tag(true)
                Text("Want").tag(false)
            }
            .pickerStyle(.segmented).labelsHidden().frame(width: 260)
            HStack(alignment: .bottom, spacing: 12) {
                VStack(alignment: .leading) {
                    Text("Resource").font(.caption).foregroundStyle(Theme.muted)
                    Picker("Resource", selection: $resource) {
                        Text("Compute").tag(NetResource.compute)
                        Text("Storage").tag(NetResource.storage)
                    }
                    .labelsHidden().frame(width: 120)
                }
                VStack(alignment: .leading) {
                    Text("Units").font(.caption).foregroundStyle(Theme.muted)
                    TextField("40", text: $units).textFieldStyle(.roundedBorder).frame(width: 90)
                }
                VStack(alignment: .leading) {
                    Text(selling ? "Price, minor units" : "Most you will pay, minor units").font(.caption).foregroundStyle(Theme.muted)
                    TextField("2400", text: $price).textFieldStyle(.roundedBorder).frame(width: 150)
                }
                if selling {
                    VStack(alignment: .leading) {
                        Text("Buyer NodeID (64 hex digits)").font(.caption).foregroundStyle(Theme.muted)
                        TextField("from the buyer's P2P Mesh page", text: $node).textFieldStyle(.roundedBorder)
                    }
                }
            }
            HStack {
                Text((try? command.text()) ?? AppStore.explain(Result { try command.text() }.failureError))
                    .font(.system(size: 13, design: .monospaced))
                    .foregroundStyle((try? command.text()) == nil ? Theme.warn : Theme.gold)
                    .padding(10)
                    .background(Theme.inset, in: RoundedRectangle(cornerRadius: 8))
                Spacer()
                Button(selling ? "Post sell offer" : "Post want") {
                    let c = command
                    Task {
                        if await store.net(c), let t = try? c.text() {
                            posted.insert(t, at: 0)
                            units = ""
                            price = ""
                        }
                    }
                }
                .buttonStyle(.borderedProminent)
                .disabled((try? command.text()) == nil || store.state?.net.bound != true)
            }
            if store.state?.net.bound != true {
                Text("Turn the network on in P2P Mesh to trade.").font(.caption).foregroundStyle(Theme.muted)
            }
            if !posted.isEmpty {
                Text("Posted from this window").font(.caption).foregroundStyle(Theme.muted)
                ForEach(posted.prefix(5), id: \.self) { Text($0).font(.system(size: 12, design: .monospaced)) }
            }
        }
    }

    private var openOffers: some View {
        let o = store.offers
        let wants = o?.wants ?? []
        let out = o?.outstanding ?? []
        return Panel(title: "Open offers") {
            KV(key: "Sharing", value: "\(o?.share.compute ?? "0") compute · \(o?.share.storage ?? "0") storage")
            if wants.isEmpty && out.isEmpty {
                Text("No open wants and nothing waiting on a peer.").font(.callout).foregroundStyle(Theme.muted)
            }
            ForEach(wants) { w in
                KV(key: "Want \(w.resource)", value: "\(w.unitsLeft) left, at most \(w.maxPrice) each")
            }
            ForEach(out) { r in
                VStack(alignment: .leading, spacing: 2) {
                    Text(r.role == "sell" ? "Offered \(r.units) units, waiting for the buyer"
                                          : "Bought \(r.units) units, waiting for the seller")
                        .font(.callout)
                    Text("\(r.peer.prefix(16))… · sent \(r.tries) time(s)")
                        .font(.system(size: 11, design: .monospaced)).foregroundStyle(Theme.muted)
                }
            }
        }
    }

    private var market: some View {
        let n = store.state?.net
        return Panel(title: "Market") {
            KV(key: "Open wants", value: grouped(n?.wants ?? 0))
            KV(key: "Trades", value: grouped(n?.trades ?? 0))
            KV(key: "Receipts refused", value: grouped(n?.receiptsRefused ?? 0))
            KV(key: "Units imported", value: grouped(n?.imported ?? 0))
            KV(key: "Conserved", value: n?.conserved == false ? "no" : "yes",
               color: n?.conserved == false ? Theme.warn : Theme.good)
        }
    }

    private var settlement: some View {
        let s = store.state
        let halted = (s?.ledgerHalt ?? 0) != 0
        return Panel(title: "Settlement", trailing: halted ? "Halted (reason \(s!.ledgerHalt))" : "Running, not halted") {
            LazyVGrid(columns: [GridItem(.adaptive(minimum: 150), spacing: 12)], spacing: 12) {
                Stat(value: grouped(s?.ledgerPostings ?? 0), label: "Ledger postings")
                Stat(value: grouped(s?.ledgerSynced ?? 0), label: "Cycles synced")
                Stat(value: grouped(s?.fees ?? 0), label: "Fees")
                Stat(value: grouped(s?.levied ?? 0), label: "Levied")
                Stat(value: grouped(s?.bountiesReturned ?? 0), label: "Bounties returned")
                Stat(value: "\(grouped(s?.ledgerAudited ?? 0)) / \(grouped(s?.ledgerIssues ?? 0))", label: "Audited / issues")
            }
            Text("Where the 0.08889% fee goes").font(.caption).foregroundStyle(Theme.muted)
            GeometryReader { g in
                HStack(spacing: 0) {
                    Theme.floor.frame(width: g.size.width * 0.50)
                    Theme.gold.frame(width: g.size.width * 0.25)
                    Theme.good.frame(width: g.size.width * 0.15)
                    Theme.emotional.frame(width: g.size.width * 0.10)
                }
            }
            .frame(height: 12)
            .clipShape(RoundedRectangle(cornerRadius: 6))
            .accessibilityLabel("Reserve floor 50%, V-Bill dividend 25%, node bounties 15%, regenerative 10%")
            HStack(spacing: 16) {
                Legend(color: Theme.floor, text: "Reserve floor 50%")
                Legend(color: Theme.gold, text: "V-Bill dividend 25%")
                Legend(color: Theme.good, text: "Node bounties 15%")
                Legend(color: Theme.emotional, text: "Regenerative 10%")
            }
            .font(.caption)
        }
    }
}

private extension Result where Failure == Error {
    var failureError: Error {
        if case .failure(let e) = self { return e }
        return NetCommand.Invalid.zero
    }
}
