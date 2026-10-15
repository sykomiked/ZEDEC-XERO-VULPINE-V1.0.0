// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// P2P Mesh: net.command (off / lan / online / peer / share), swarm.state.net
// and net.peers.
import SwiftUI
import ZXVRPC

@MainActor
struct MeshView: View {
    @EnvironmentObject var store: AppStore
    @State private var peer = ""
    @State private var shareCompute = ""
    @State private var shareStorage = ""

    private var net: NetStatus? { store.state?.net }
    private var mode: NetMode { NetMode(rawValue: net?.mode ?? "off") ?? .off }

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            PageHeader(title: "P2P Mesh",
                       subtitle: "The Vinea peer network. It is off by default, and nothing leaves this Mac until you turn it on.") {
                Picker("Network mode", selection: Binding(get: { mode }, set: { m in
                    Task { await store.net(.mode(m)) }
                })) {
                    Text("Off").tag(NetMode.off)
                    Text("LAN").tag(NetMode.lan)
                    Text("Online").tag(NetMode.online)
                }
                .pickerStyle(.segmented)
                .labelsHidden()
                .frame(width: 300)
            }
            Text(help)
                .padding(12)
                .frame(maxWidth: .infinity, alignment: .leading)
                .background(Theme.panel, in: RoundedRectangle(cornerRadius: 10))
            if let e = net?.error, !e.isEmpty {
                Text("Network error: \(e)").foregroundStyle(Theme.warn)
            }
            HStack(alignment: .top, spacing: 20) {
                node
                traffic
            }
            HStack(alignment: .top, spacing: 20) {
                peersAndSharing
                log
            }
            if store.has("net.peers") {
                peerList
            } else {
                NotServed(what: "Peers", method: "net.peers",
                          detail: "A row per peer with its NodeID, address, last contact and traffic. zxvd reports only the peer count today.")
            }
        }
    }

    private var help: String {
        switch mode {
        case .off: return "Off: no socket is open. The swarm runs on this Mac only."
        case .lan: return "LAN: listening on your local network only. Good for two Macs in the same room."
        case .online: return "Online: reachable by peers you add. Signed updates are checked while online."
        }
    }

    private var node: some View {
        Panel(title: "This node", trailing: net?.bound == true ? "Bound" : "Not running") {
            KV(key: "Node ID", value: (net?.nodeId).flatMap { $0.isEmpty ? nil : $0 } ?? "\u{2014}")
            KV(key: "Listening on", value: net?.bound == true ? "\(net!.bind):\(net!.port)" : "\u{2014}")
            KV(key: "Handshake", value: "ML-KEM-1024 + ML-DSA-87")
            Text("Full NodeID").font(Theme.caption).foregroundStyle(Theme.muted)
            Text((net?.id).flatMap { $0.isEmpty ? nil : $0 } ?? "The NodeID appears when the network is on.")
                .font(Theme.mono(12))
                .textSelection(.enabled)
                .padding(10)
                .frame(maxWidth: .infinity, alignment: .leading)
                .background(Theme.inset, in: RoundedRectangle(cornerRadius: 8))
        }
    }

    private var traffic: some View {
        Panel(title: "Traffic") {
            LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 12), count: 3), spacing: 12) {
                Stat(value: grouped(net?.peers ?? 0), label: "Peers")
                Stat(value: grouped(net?.inbound ?? 0), label: "Datagrams in")
                Stat(value: grouped(net?.outbound ?? 0), label: "Datagrams out")
                Stat(value: grouped(net?.refused ?? 0), label: "Refused")
                Stat(value: grouped(net?.policyDrops ?? 0), label: "Policy drops")
                Stat(value: grouped(net?.sendErrors ?? 0), label: "Send errors")
            }
            Label("Imports \(net?.conserved == false ? "NOT conserved" : "conserved") · \(grouped(net?.imported ?? 0)) units imported",
                  systemImage: net?.conserved == false ? "exclamationmark.triangle" : "checkmark")
                .foregroundStyle(net?.conserved == false ? Theme.warn : Theme.good)
                .font(Theme.callout)
        }
    }

    private var peersAndSharing: some View {
        Panel(title: "Add a peer") {
            HStack {
                TextField("192.168.1.20:\(NetCommand.defaultPort)", text: $peer).textFieldStyle(.roundedBorder)
                Button("Add peer") {
                    let p = peer.trimmingCharacters(in: .whitespaces)
                    Task { if await store.net(.peer(p)) { peer = "" } }
                }
                .buttonStyle(.borderedProminent)
                .disabled(mode == .off)
            }
            if mode == .off {
                Text("Turn on LAN or Online first.").font(Theme.caption).foregroundStyle(Theme.muted)
            }
            Divider()
            Text("SHARE WITH THE MESH").font(Theme.ui(12)).kerning(1).foregroundStyle(Theme.muted)
            HStack {
                TextField("Compute units", text: $shareCompute).textFieldStyle(.roundedBorder)
                TextField("Storage units", text: $shareStorage).textFieldStyle(.roundedBorder)
                Button("Update sharing") {
                    let c = UInt64(shareCompute.trimmingCharacters(in: .whitespaces)) ?? 0
                    let s = UInt64(shareStorage.trimmingCharacters(in: .whitespaces)) ?? 0
                    Task { await store.net(.share(compute: c, storage: s)) }
                }
            }
            Text("Now sharing \(grouped(net?.shareCompute ?? 0)) compute and \(grouped(net?.shareStorage ?? 0)) storage units. 0 and 0 stops sharing.")
                .font(Theme.callout).foregroundStyle(Theme.muted)
        }
    }

    private var peerList: some View {
        let list = store.peers?.peers ?? []
        return Panel(title: "Peers", trailing: "\(grouped(store.peers?.count ?? 0)) verified") {
            if list.isEmpty {
                Text(mode == .off ? "Turn on LAN or Online to see peers."
                                  : "No peer has answered yet. Add one by address above.")
                    .foregroundStyle(Theme.muted)
            }
            ForEach(list) { p in
                VStack(alignment: .leading, spacing: 4) {
                    HStack {
                        Text(p.id).font(Theme.mono(11)).foregroundStyle(Theme.gold)
                            .lineLimit(1).truncationMode(.middle).textSelection(.enabled)
                        Spacer()
                        Text(p.addr.isEmpty ? "\u{2014}" : p.addr).font(Theme.mono(12))
                    }
                    HStack(spacing: 12) {
                        Text("Seen " + (p.lastSeen.map { $0.formatted(date: .omitted, time: .standard) } ?? "\u{2014}"))
                        Text(p.inBook ? (p.member ? "Commons member" : "In your mesh book") : "No trades yet")
                        Text("Held \(p.held) · delivered on credit \(p.receivable)")
                        if p.pending > 0 {
                            Text("\(p.pending) receipt(s) pending").foregroundStyle(Theme.warn)
                        }
                        if p.ubh { Text("UBH-168") }
                    }
                    .font(Theme.caption).foregroundStyle(Theme.muted)
                }
                .padding(8)
                .background(Theme.inset, in: RoundedRectangle(cornerRadius: 8))
            }
        }
    }

    private var log: some View {
        Panel(title: "Network log") {
            if store.netLog.isEmpty {
                Text("Commands you send to zxvd appear here.").foregroundStyle(Theme.muted)
            }
            ForEach(store.netLog.prefix(8)) { l in
                HStack(spacing: 10) {
                    Text(l.command).foregroundStyle(Theme.gold)
                    Text(l.result).foregroundStyle(l.ok ? Theme.text : Theme.warn)
                    Spacer()
                }
                .font(Theme.mono(12))
                .padding(8)
                .background(Theme.inset, in: RoundedRectangle(cornerRadius: 8))
            }
        }
    }
}
