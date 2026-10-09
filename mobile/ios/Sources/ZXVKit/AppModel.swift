// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import Foundation
import SwiftUI
import UIKit

public enum Tab: String, CaseIterable, Identifiable {
    case assistant = "Assistant", feed = "Feed", calls = "Calls", wallet = "Wallet", network = "Devices"
    public var id: String { rawValue }
    var icon: String {
        switch self {
        case .assistant: return "sparkles"
        case .feed: return "person.3"
        case .calls: return "phone"
        case .wallet: return "creditcard"
        case .network: return "dot.radiowaves.left.and.right"
        }
    }
}

public struct ChatMessage: Identifiable {
    public let id = UUID()
    public let fromUser: Bool
    public var text: String
    public let place: String
}

public struct SasPrompt: Identifiable {
    public let peer: Data
    public let sas: UInt32
    public var id: Data { peer }
    var digits: String {
        let s = String(format: "%06u", sas)
        return String(s.prefix(3)) + " " + String(s.suffix(3))
    }
}

public struct MoneyPrompt: Identifiable {
    public let id = UUID()
    public let amountMinor: UInt64
    public let rail: UInt32
    public let memo: String
}

/// UI state over the core, mirroring the Android AppState. Main thread only.
@MainActor
public final class AppModel: ObservableObject, CoreDelegate {
    @Published public var tab: Tab = .assistant
    @Published public var messages: [ChatMessage] = []
    @Published public var devices: [Device] = []
    @Published public var models: [String] = []
    @Published public var invitation: (qr: String, code: String)?
    @Published public var sas: SasPrompt?
    @Published public var money: MoneyPrompt?
    @Published public var status = "starting"
    private var pending: [UInt32: Int] = [:]
    private var lan: LanTransport?
    private var timer: Timer?
    private let core = Core.shared

    public init() {}

    public func start() {
        core.delegate = self
        let t = LanTransport { [weak self] f in _ = self?.core.receive(f) }
        lan = t
        try? t.start()
        let rc = core.start(seed: SeedStore.load(), name: UIDevice.current.name, role: 2, flags: 3)
        if rc == 0 { _ = core.createMesh() }
        let ram = UInt32(ProcessInfo.processInfo.physicalMemory / (1024 * 1024))
        UIDevice.current.isBatteryMonitoringEnabled = true
        let batt = Int32(max(0, UIDevice.current.batteryLevel) * 100)
        _ = core.setCaps(ramMb: ram, compute: 1, batteryPct: batt,
                         charging: UIDevice.current.batteryState == .charging, net: 1)
        refresh()
        status = rc == 0 ? "on this phone" : "core error \(rc)"
        timer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.core.tick() }
        }
    }

    func refresh() {
        devices = core.devices()
        models = core.localModels()
    }

    public func send(_ text: String) {
        guard !text.isEmpty else { return }
        messages.append(ChatMessage(fromUser: true, text: text, place: ""))
        let r = core.route(kind: 1, modelClass: 2) // prompt, large model
        switch r.route {
        case 1:
            let id = core.prompt(target: r.target, text: text)
            if id > 0 {
                pending[UInt32(id)] = messages.count
                messages.append(ChatMessage(fromUser: false, text: "", place: "home computer"))
            } else { local(text) }
        case 2, 3: local(text)
        default:
            messages.append(ChatMessage(fromUser: false, text: "No model can run this here; pair a home computer or buy capacity in Devices.", place: ""))
        }
    }

    private func local(_ text: String) {
        // On-device inference is not wired yet (see docs/MOBILE_AND_DEVICES.md).
        messages.append(ChatMessage(fromUser: false, text: "(on-device model not wired yet)", place: "this phone"))
    }

    public func invite() { invitation = core.invite(role: 1, flags: 0) }
    public func join(code: String) { _ = core.joinCode(code) }
    public func join(qr: String) { _ = core.joinQr(qr) }
    public func answerSas(_ match: Bool) {
        if let s = sas { _ = core.confirmSas(peer: s.peer, match: match) }
        sas = nil
    }
    public func answerMoney(_ ok: Bool) { _ = core.moneyAnswer(approve: ok); money = nil }
    public func revoke(_ d: Device) { _ = core.revoke(d.id); refresh() }
    public func tithe(_ minor: UInt64) -> UInt64 { core.tithe(minor) }

    // ---- CoreDelegate (called on the main thread from inside core calls) ----
    nonisolated public func coreSend(to: Data, frame: Data) -> Int32 {
        MainActor.assumeIsolated { lan?.send(to: to, frame: frame) ?? -1 }
    }

    nonisolated public func coreEvent(kind: Int32, peer: Data, sas: UInt32, value: UInt64) {
        Task { @MainActor in
            switch kind {
            case ZXV_EV_SAS: self.sas = SasPrompt(peer: peer, sas: sas)
            case ZXV_EV_PAIRED: self.invitation = nil; self.status = "paired"
            case ZXV_EV_PAIR_FAILED: self.status = "pairing failed"
            case ZXV_EV_PEER_UP: self.status = "home computer connected"
            case ZXV_EV_PEER_DOWN: self.status = "on this phone"
            default: break
            }
            self.refresh()
        }
    }

    nonisolated public func coreReply(reqId: UInt32, text: String, final: Bool) {
        Task { @MainActor in
            guard let i = self.pending[reqId], i < self.messages.count else { return }
            self.messages[i].text += text
            if final { self.pending[reqId] = nil }
        }
    }

    nonisolated public func coreMoney(amountMinor: UInt64, rail: UInt32, memo: String) {
        Task { @MainActor in self.money = MoneyPrompt(amountMinor: amountMinor, rail: rail, memo: memo) }
    }
}
