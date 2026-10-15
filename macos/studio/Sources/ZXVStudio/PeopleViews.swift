// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The people screens (kernel/arch/hosted/zxvd_people.h): Feed (social.*),
// Calls (call.*, signalling only: no sound or video in this build),
// Translate (xlate.*, through the same model slot as swarm.ask; no model
// means an error, never invented text), Devices (devmesh.*), and simple
// mode's one-tap Ask and Pay (swarm.ask, ledger.post). Simple mode only
// hides details (ids, hashes, weights); every request and confirmation is
// the same either way.
import SwiftUI
import ZXVRPC

private func short(_ hex: String?) -> String {
    guard let h = hex else { return "" }
    return h.count > 12 ? String(h.prefix(12)) + "\u{2026}" : h
}

private func words(_ s: String?) -> String { (s ?? "").replacingOccurrences(of: "_", with: " ") }

/// Re-reads every two seconds while the screen is open.
private func polling(_ load: @escaping () async -> Void) async {
    while !Task.isCancelled {
        await load()
        try? await Task.sleep(nanoseconds: 2_000_000_000)
    }
}

private struct Hint: View {
    let text: String
    var bad = false
    var body: some View {
        Text(text).font(.callout).foregroundStyle(bad ? Theme.warn : Theme.muted)
    }
}

// MARK: Ask and Pay (simple mode)

@MainActor
struct SimpleView: View {
    @EnvironmentObject var store: AppStore
    @State private var question = ""
    @State private var from: Int?
    @State private var to: Int?
    @State private var amount = ""
    @State private var reference = ""
    @State private var payLine: (Bool, String)?
    @State private var confirming = false

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            PageHeader(title: "Ask and Pay", subtitle: "One tap to ask the companion or pay between your accounts.")
            HStack(alignment: .top, spacing: 20) {
                Panel(title: "Ask") {
                    TextField("Ask the companion anything", text: $question, axis: .vertical)
                        .textFieldStyle(.roundedBorder).lineLimit(3...6)
                    Button(store.asking ? "Asking" : "Ask") {
                        let q = question
                        question = ""
                        Task { await store.ask(q) }
                    }
                    .buttonStyle(.borderedProminent).controlSize(.large)
                    .disabled(store.asking || question.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
                    if let a = store.chat.last(where: { !$0.mine }) {
                        Text(a.text).textSelection(.enabled).padding(10)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .background(Theme.inset, in: RoundedRectangle(cornerRadius: 8))
                    }
                }
                pay
            }
        }
        .task { await store.refreshLedger() }
    }

    private var pay: some View {
        Panel(title: "Pay") {
            if store.accounts.count < 2 {
                Text("Pay needs two accounts in the same currency. Open them in the Sicily Ledger (turn simple mode off in Settings).")
                    .foregroundStyle(Theme.muted)
            } else {
                Picker("From", selection: $from) {
                    Text("Choose").tag(Int?.none)
                    ForEach(store.accounts, id: \.account) { a in Text("\(a.name) (\(a.currency))").tag(Int?.some(a.account)) }
                }
                Picker("To", selection: $to) {
                    Text("Choose").tag(Int?.none)
                    ForEach(store.accounts, id: \.account) { a in Text("\(a.name) (\(a.currency))").tag(Int?.some(a.account)) }
                }
                TextField("Amount, like 12.50", text: $amount).textFieldStyle(.roundedBorder)
                TextField("Reference (optional; each payment needs its own)", text: $reference).textFieldStyle(.roundedBorder)
                Button("Pay") { confirming = true }
                    .buttonStyle(.borderedProminent).controlSize(.large)
                    .disabled(from == nil || to == nil || from == to || amount.isEmpty)
                    .confirmationDialog(confirmText, isPresented: $confirming) {
                        Button("Pay") { Task { await send() } }
                        Button("Cancel", role: .cancel) {}
                    }
                Hint(text: payLine?.1 ?? "You confirm every payment. Amounts are exact: fractions are refused, never rounded.",
                     bad: payLine?.0 == false)
            }
        }
    }

    private var confirmText: String {
        let ccy = store.accounts.first(where: { $0.account == from })?.currency ?? ""
        let a = from.map { store.name(of: $0) } ?? ""
        let b = to.map { store.name(of: $0) } ?? ""
        return "Pay \(amount) \(ccy) from \(a) to \(b)?"
    }

    private func send() async {
        guard let f = from, let t = to else { return }
        await store.select(f)
        let ref = reference.trimmingCharacters(in: .whitespaces)
        let r = await store.post(to: t, amount: amount,
                                 reference: ref.isEmpty ? "pay-\(UInt64(Date().timeIntervalSince1970 * 1000))" : ref)
        payLine = r.0 ? (true, store.simpleMode ? "Paid." : r.1) : r
        if r.0 { amount = ""; reference = "" }
    }
}

// MARK: Feed

@MainActor
struct FeedView: View {
    @EnvironmentObject var store: AppStore
    @State private var feed: JSON?
    @State private var groups: [JSON] = []
    @State private var draft = ""
    @State private var group = ""
    @State private var replyTo: String?
    @State private var newGroup = ""
    @State private var line: (Bool, String)?

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            PageHeader(title: "Feed", subtitle: "Signed posts and groups. Ranked by what each post adds, never by likes.")
            HStack(alignment: .top, spacing: 20) {
                VStack(spacing: 16) { composer; groupList }.frame(width: 360)
                items
            }
        }
        .task { await polling(load) }
    }

    private func load() async {
        if let f = try? await store.rpc("social.feed", ["limit": 50]) { feed = f }
        if let g = try? await store.rpc("social.groups") { groups = g["groups"]?.array ?? [] }
    }

    private func act(_ method: String, _ params: [String: JSON], ok: String? = nil) async {
        do {
            _ = try await store.rpc(method, params)
            if let ok { line = (true, ok) }
        } catch {
            line = (false, "\(error)")
        }
        await load()
    }

    private func isMember(_ g: JSON) -> Bool { ["member", "admin", "owner"].contains(g["my_role"]?.string ?? "") }

    private var composer: some View {
        Panel(title: replyTo == nil ? "Post" : "Reply") {
            TextField(replyTo == nil ? "Write a post" : "Write a reply", text: $draft, axis: .vertical)
                .textFieldStyle(.roundedBorder).lineLimit(3...8)
            if replyTo == nil && !store.simpleMode {
                Picker("Group", selection: $group) {
                    Text("Public").tag("")
                    ForEach(Array(groups.filter(isMember).enumerated()), id: \.offset) { _, g in
                        Text(g["name"]?.string ?? "").tag(g["id"]?.string ?? "")
                    }
                }
            }
            HStack {
                Button(replyTo == nil ? "Post" : "Reply") {
                    var p: [String: JSON] = ["text": .string(draft)]
                    if let r = replyTo { p["reply_to"] = .string(r) } else if !group.isEmpty { p["group"] = .string(group) }
                    Task {
                        await act("social.post", p, ok: "Posted.")
                        if line?.0 == true { draft = ""; replyTo = nil }
                    }
                }
                .buttonStyle(.borderedProminent)
                .disabled(draft.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
                if replyTo != nil { Button("Cancel reply") { replyTo = nil } }
            }
            if let l = line { Hint(text: l.1, bad: !l.0) }
        }
    }

    private var groupList: some View {
        Panel(title: "Groups") {
            if groups.isEmpty { Text("No groups yet.").foregroundStyle(Theme.muted) }
            ForEach(Array(groups.enumerated()), id: \.offset) { _, g in
                HStack {
                    VStack(alignment: .leading, spacing: 2) {
                        Text(g["name"]?.string ?? "").fontWeight(.medium)
                        Text(groupLine(g))
                            .font(.caption).foregroundStyle(Theme.muted)
                    }
                    Spacer()
                    if g["my_role"]?.string != "owner", let id = g["id"]?.string {
                        Button(isMember(g) ? "Leave" : "Join") {
                            Task { await act(isMember(g) ? "social.leave" : "social.join", ["group": .string(id)]) }
                        }
                    }
                }
            }
            HStack {
                TextField("New group name", text: $newGroup).textFieldStyle(.roundedBorder)
                Button("Create") {
                    let n = newGroup
                    Task {
                        await act("social.group_create", ["name": .string(n), "open": true], ok: "Group created.")
                        if line?.0 == true { newGroup = "" }
                    }
                }
                .disabled(newGroup.trimmingCharacters(in: .whitespaces).isEmpty)
            }
        }
    }

    private var items: some View {
        let list = feed?["items"]?.array ?? []
        return Panel(title: "Feed", trailing: store.simpleMode ? nil : "Me \(short(feed?["me"]?.string)) · \(feed?["held"]?.int ?? 0) held") {
            if list.isEmpty { Text("Nothing here yet.").foregroundStyle(Theme.muted) }
            ForEach(Array(list.enumerated()), id: \.offset) { _, x in
                VStack(alignment: .leading, spacing: 6) {
                    Text(header(x)).font(.caption).foregroundStyle(Theme.muted)
                    Text(x["text"]?.string ?? "").textSelection(.enabled)
                    HStack(spacing: 6) {
                        ForEach(["\u{1F44D}", "\u{2764}\u{FE0F}", "\u{1F602}"], id: \.self) { re in
                            let n = (x["reactions"]?.array ?? []).first(where: { $0["reaction"]?.string == re })?["count"]?.int
                            Button(n.map { "\(re) \($0)" } ?? re) {
                                if let id = x["id"]?.string {
                                    Task { await act("social.react", ["post": .string(id), "reaction": .string(re)]) }
                                }
                            }
                        }
                        let replies = x["replies"]?.int ?? 0
                        Button(replies > 0 ? "Reply (\(replies))" : "Reply") { replyTo = x["id"]?.string }
                    }
                    .controlSize(.small)
                }
                .padding(10)
                .frame(maxWidth: .infinity, alignment: .leading)
                .background(Theme.inset, in: RoundedRectangle(cornerRadius: 8))
            }
        }
    }

    private func groupLine(_ g: JSON) -> String {
        var s = "\(g["members"]?.int ?? 0) members"
        if g["open"]?.bool == false { s += " · invite only" }
        if !store.simpleMode { s += " · " + (g["my_role"]?.string ?? "") }
        return s
    }

    private func header(_ x: JSON) -> String {
        var h = x["mine"]?.bool == true ? "You" : (store.simpleMode ? "Someone" : short(x["author"]?.string))
        if x["kind"]?.string == "reply" { h += " · reply" }
        if let gid = x["group"]?.string {
            h += " · " + (groups.first(where: { $0["id"]?.string == gid })?["name"]?.string ?? "a group")
        }
        if !store.simpleMode, let s = x["score"]?.int { h += " · score \(s)" }
        return h
    }
}

// MARK: Calls

@MainActor
struct CallsView: View {
    @EnvironmentObject var store: AppStore
    @State private var status: JSON?
    @State private var peer = ""
    @State private var line: (Bool, String)?

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            PageHeader(title: "Calls", subtitle: "Call signalling between ZXV members. This build carries no sound or video yet.")
            HStack(alignment: .top, spacing: 20) {
                Panel(title: "Place a call") {
                    HStack {
                        TextField("Member number, or loopback", text: $peer).textFieldStyle(.roundedBorder)
                        Button("Call") { Task { await start() } }
                            .buttonStyle(.borderedProminent)
                            .disabled(peer.trimmingCharacters(in: .whitespaces).isEmpty)
                    }
                    Hint(text: line?.1 ?? "\"loopback\" rings a test line inside this Mac.", bad: line?.0 == false)
                    if !store.simpleMode, let s = status {
                        KV(key: "My member number", value: "\(s["self_id"]?.int ?? 0)")
                        KV(key: "Loopback number", value: "\(s["loopback_id"]?.int ?? 0)")
                        KV(key: "Media", value: s["media"]?.string ?? "\u{2014}")
                        KV(key: "Waiting to send", value: "\(s["outbox"]?.int ?? 0)")
                    }
                }
                .frame(width: 400)
                lines
            }
        }
        .task { await polling(load) }
    }

    private func load() async {
        if let s = try? await store.rpc("call.status") { status = s }
    }

    private func start() async {
        let v = peer.trimmingCharacters(in: .whitespaces)
        let p: JSON = v == "loopback" ? JSON.string(v) : (Int64(v).map { JSON.int($0) } ?? JSON.string(v))
        do {
            _ = try await store.rpc("call.start", ["peer": p])
            line = nil
        } catch {
            line = (false, "\(error)")
        }
        await load()
    }

    private func act(_ method: String, _ n: Int64) async {
        do { _ = try await store.rpc(method, ["line": .int(n)]) } catch { line = (false, "\(error)") }
        await load()
    }

    private func lineTitle(_ l: JSON) -> String {
        var s = l["loopback"]?.bool == true ? "Loopback line" : "Line \(l["line"]?.int ?? 0)"
        s += l["outgoing"]?.bool == true ? " · outgoing" : " · incoming"
        let missed = l["missed"]?.int ?? 0
        if missed > 0 { s += " · \(missed) missed" }
        return s
    }

    private func lineState(_ l: JSON) -> String {
        let st = l["state"]?.string ?? ""
        var s = "Member \(l["peer"]?.int ?? 0) · " + words(st)
        if st == "ended" { s += " (" + words(l["end_reason"]?.string) + ")" }
        return s
    }

    private var lines: some View {
        let shown = (status?["lines"]?.array ?? []).filter { $0["state"]?.string != "idle" || ($0["missed"]?.int ?? 0) > 0 }
        return Panel(title: "Lines") {
            if shown.isEmpty { Text("No calls.").foregroundStyle(Theme.muted) }
            ForEach(Array(shown.enumerated()), id: \.offset) { _, l in
                let n = l["line"]?.int ?? 0
                let st = l["state"]?.string ?? ""
                HStack {
                    VStack(alignment: .leading, spacing: 2) {
                        Text(lineTitle(l)).font(.caption).foregroundStyle(Theme.muted)
                        Text(lineState(l))
                        if !store.simpleMode, let id = l["call_id"]?.string {
                            Text("call \(id)").font(.system(size: 11, design: .monospaced)).foregroundStyle(Theme.muted)
                        }
                    }
                    Spacer()
                    if st == "incoming" { Button("Answer") { Task { await act("call.accept", n) } }.buttonStyle(.borderedProminent) }
                    if st != "idle" && st != "ended" { Button("Hang up") { Task { await act("call.hangup", n) } } }
                }
                .padding(10)
                .background(Theme.inset, in: RoundedRectangle(cornerRadius: 8))
            }
        }
    }
}

// MARK: Translate

@MainActor
struct TranslateView: View {
    @EnvironmentObject var store: AppStore
    @State private var text = ""
    @State private var to = "en"
    @State private var languages: [JSON] = []
    @State private var detected: String?
    @State private var result: (Bool, String)?
    @State private var note: String?
    @State private var busy = false

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            PageHeader(title: "Translate", subtitle: "Machine translation on this Mac, through the companion's model. No model, no translation.")
            HStack(alignment: .top, spacing: 20) {
                Panel(title: "Text") {
                    TextField("Text to translate", text: $text, axis: .vertical)
                        .textFieldStyle(.roundedBorder).lineLimit(4...12)
                    Picker("Into", selection: $to) {
                        ForEach(Array(languages.enumerated()), id: \.offset) { _, l in Text(l["name"]?.string ?? "").tag(l["tag"]?.string ?? "") }
                    }
                    Button(busy ? "Translating" : "Translate") { Task { await go() } }
                        .buttonStyle(.borderedProminent)
                        .disabled(busy || text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
                    if let d = detected { Hint(text: d) }
                }
                Panel(title: "Result") {
                    Text(result?.1 ?? "Nothing yet.").textSelection(.enabled)
                        .foregroundStyle(result?.0 == false ? Theme.warn : Theme.text)
                    if let n = note { Hint(text: n) }
                }
            }
        }
        .task {
            if let l = try? await store.rpc("xlate.languages") { languages = l["languages"]?.array ?? [] }
        }
    }

    private func go() async {
        busy = true
        defer { busy = false }
        note = nil
        if let d = try? await store.rpc("xlate.detect", ["text": .string(text)]) {
            detected = d["language"]?.string == "und" ? "Language not recognised."
                : "Looks like \(d["name"]?.string ?? "")" + (store.simpleMode ? "" : " (\(d["confidence"]?.int ?? 0)%)")
        }
        do {
            let r = try await store.rpc("xlate.translate", ["text": .string(text), "to": .string(to)], timeoutMs: 300_000)
            result = (true, r["text"]?.string ?? "")
            note = r["translated"]?.bool == true ? "Machine translation: it may be wrong." : "Already in that language: not translated."
        } catch {
            result = (false, "\(error)")
        }
    }
}

// MARK: Devices

@MainActor
struct DevicesView: View {
    @EnvironmentObject var store: AppStore
    @State private var status: JSON?
    @State private var devices: [JSON] = []
    @State private var invite: JSON?
    @State private var code = ""
    @State private var line: (Bool, String)?
    @State private var removing: JSON?

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            PageHeader(title: "Devices", subtitle: "Your own computers and phones, paired with a code and a six-digit check.")
            HStack(alignment: .top, spacing: 20) {
                VStack(spacing: 16) { this; add }.frame(width: 420)
                list
            }
        }
        .task { await polling(load) }
    }

    private func load() async {
        if let s = try? await store.rpc("devmesh.status") { status = s }
        if let d = try? await store.rpc("devmesh.devices") { devices = d["devices"]?.array ?? [] }
    }

    private func call(_ method: String, _ params: [String: JSON] = [:]) async -> JSON? {
        defer { Task { await load() } }
        do { return try await store.rpc(method, params) } catch { line = (false, "\(error)"); return nil }
    }

    private var this: some View {
        Panel(title: "This device") {
            KV(key: "Name", value: status?["name"]?.string ?? "\u{2014}")
            KV(key: "Role", value: status?["role"]?.string ?? "\u{2014}")
            KV(key: "In a mesh", value: status?["joined"]?.bool == true ? "yes" : "no")
            if !store.simpleMode {
                KV(key: "Device id", value: status?["self"]?.string ?? "\u{2014}")
                KV(key: "Security level", value: status?["level"]?.string ?? "\u{2014}")
                KV(key: "Roster version", value: status?["roster_version"]?.string ?? "\u{2014}")
            }
            if let e = status?["save_error"]?.string { Hint(text: e, bad: true) }
            if let p = status?["pairing"], let dev = p["device"]?.string, let sas = p["sas"]?.string {
                Divider()
                Text("Check that your other device shows the same number:").font(.callout)
                Text(sas).font(.system(size: 30, weight: .semibold, design: .monospaced)).kerning(4)
                HStack {
                    Button("They match") {
                        Task {
                            if let r = await call("devmesh.confirm_sas", ["device": .string(dev), "match": true]) {
                                line = (true, r["confirmed"]?.bool == true ? "Device added." : "Pairing stopped.")
                            }
                        }
                    }
                    .buttonStyle(.borderedProminent)
                    Button("They differ") { Task { _ = await call("devmesh.confirm_sas", ["device": .string(dev), "match": false]) } }
                }
            }
        }
    }

    private var add: some View {
        Panel(title: "Add a device") {
            Button("Invite a device") { Task { invite = await call("devmesh.invite") } }
            if let i = invite, let c = i["code"]?.string {
                Text("On the other device, enter this code (valid \((i["expires_in_ms"]?.int ?? 0) / 60000) minutes):").font(.callout)
                Text(c).font(.system(size: 26, weight: .semibold, design: .monospaced)).kerning(3).textSelection(.enabled)
                if !store.simpleMode, let qr = i["qr"]?.string {
                    Text(qr).font(.system(size: 10, design: .monospaced)).foregroundStyle(Theme.muted)
                        .textSelection(.enabled).lineLimit(4)
                }
            }
            HStack {
                TextField("Code from your other device (XXXXX-XXXXX)", text: $code).textFieldStyle(.roundedBorder)
                Button("Join") {
                    let c = code.trimmingCharacters(in: .whitespaces).uppercased()
                    let p: [String: JSON] = c.count > 11 ? ["qr": JSON.string(c)] : ["code": JSON.string(c)]
                    Task {
                        if let r = await call("devmesh.join", p) {
                            line = (true, r["next"]?.string ?? "Joining.")
                        }
                    }
                }
                .disabled(code.trimmingCharacters(in: .whitespaces).isEmpty)
            }
            Hint(text: line?.1 ?? "Pairing two machines also needs a carrier (the phone link); none is wired to the Studio in this build.",
                 bad: line?.0 == false)
        }
    }

    private func deviceLine(_ d: JSON) -> String {
        var s = d["role"]?.string ?? ""
        if d["admin"]?.bool == true { s += " · admin" }
        s += " · " + (d["status"]?.string ?? "")
        if d["self"]?.bool == true { s += " · this device" }
        return s
    }

    private var list: some View {
        Panel(title: "Devices") {
            if devices.isEmpty { Text("Only this device.").foregroundStyle(Theme.muted) }
            ForEach(Array(devices.enumerated()), id: \.offset) { _, d in
                HStack {
                    VStack(alignment: .leading, spacing: 2) {
                        Text(d["name"]?.string ?? "").fontWeight(.medium)
                        Text(deviceLine(d))
                            .font(.caption).foregroundStyle(Theme.muted)
                        if !store.simpleMode {
                            Text(d["id"]?.string ?? "").font(.system(size: 11, design: .monospaced)).foregroundStyle(Theme.muted)
                        }
                    }
                    Spacer()
                    if d["self"]?.bool != true && d["status"]?.string == "active" {
                        Button("Remove", role: .destructive) { removing = d }
                    }
                }
                .padding(10)
                .background(Theme.inset, in: RoundedRectangle(cornerRadius: 8))
            }
        }
        .confirmationDialog("Remove \(removing?["name"]?.string ?? "this device")? It loses access at once.",
                            isPresented: Binding(get: { removing != nil }, set: { if !$0 { removing = nil } })) {
            Button("Remove", role: .destructive) {
                if let id = removing?["id"]?.string { Task { _ = await call("devmesh.revoke", ["device": .string(id)]) } }
                removing = nil
            }
            Button("Cancel", role: .cancel) { removing = nil }
        }
    }
}
