// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// ZXV Studio's entry point. `ZXVStudio --smoke [SOCKET]` connects (starting
// the bundled zxvd if needed), reads the calls every view makes once, prints
// a summary and exits 0, or 1 on any failure: CI runs it on the built .app.
import AppKit
import Foundation
import SwiftUI
import ZXVRPC

@MainActor
@main
enum StudioMain {
    static func main() {
        let args = CommandLine.arguments
        if let i = args.firstIndex(of: "--smoke") {
            let sock = args.count > i + 1 ? args[i + 1] : ZXVDLocation.defaultSocketPath
            exit(Smoke.run(socket: sock))
        }
        ASCWFont.registerAll()
        ZXVStudioApp.main()
    }
}

enum Smoke {
    static func run(socket: String) -> Int32 {
        do {
            let (c, daemon, d) = try Connector.connect(socket: socket)
            defer { daemon?.stop() }
            print("zxvd: \(d.protocol), \(d.methods.count) methods\(daemon != nil ? ", started from the bundle" : "")")
            let v = try c.call("zxvd.version", as: Version.self)
            print("version: app \(v.app), libzxv \(v.libzxv)")
            let s = try c.call("swarm.state", as: SwarmState.self)
            print("swarm: \(s.levels) levels, \(s.agents.count) agents, \(s.tokensPerCycle) tokens per cycle, net \(s.net.mode)")
            let m = try c.call("model.info", as: ModelInfo.self)
            print("model: \(m.loaded ? m.name : "none") (\(m.status))")
            let n = try c.call("notes.list", as: Notes.self)
            print("notes: \(n.unread) unread")
            let a = try c.call("ledger.accounts", as: Accounts.self)
            let ok = try c.call("ledger.check", as: Checked.self).ok
            print("ledger: \(a.accounts.count) accounts, books \(ok ? "balanced" : "NOT balanced")")
            if d.methods.contains("phone.list") {
                let p = try c.call("phone.list", as: PhoneList.self)
                print("phones: link \(p.on ? "on" : "off"), \(p.phones.count) paired")
            }
            return ok ? 0 : 1
        } catch {
            FileHandle.standardError.write(Data("smoke failed: \(error)\n".utf8))
            return 1
        }
    }
}

struct ZXVStudioApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) var delegate
    @StateObject private var store = AppStore()

    var body: some Scene {
        WindowGroup("ZXV Studio") {
            RootView()
                .font(Theme.body)
                .environmentObject(store)
                .frame(minWidth: 980, minHeight: 680)
                .preferredColorScheme(.dark)
                .tint(Theme.gold)
                .onAppear {
                    delegate.store = store
                    store.start()
                }
        }
        .windowStyle(.hiddenTitleBar)
        .commands {
            CommandGroup(replacing: .newItem) {}
        }

        MenuBarExtra {
            MenuBarContent().environmentObject(store)
        } label: {
            Image(systemName: store.unread > 0 ? "circle.hexagongrid.fill" : "circle.hexagongrid")
        }
    }
}

@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    weak var store: AppStore?

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { false }

    func applicationWillTerminate(_ notification: Notification) {
        // a zxvd this app started stops with it; one started elsewhere keeps running
        store?.stopDaemonIfOurs()
    }
}

@MainActor
struct MenuBarContent: View {
    @EnvironmentObject var store: AppStore

    var body: some View {
        switch store.link {
        case .connected: Text("zxvd connected")
        case .connecting: Text("Connecting to zxvd")
        case .down: Text("zxvd is not running")
        }
        if let c = store.state?.companion {
            Text("Budget: \(grouped(c.remaining)) of \(grouped(c.allotted)) tokens left")
        }
        Text(store.unread == 1 ? "1 unread note" : "\(store.unread) unread notes")
        Divider()
        Button("Open ZXV Studio") {
            NSApp.activate(ignoringOtherApps: true)
            NSApp.windows.first { $0.canBecomeMain }?.makeKeyAndOrderFront(nil)
        }
        Button("Quit") { NSApp.terminate(nil) }.keyboardShortcut("q")
    }
}
