// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Commons: the owner's signed posts, replies, reactions and follows through
// zxvd's chat.* methods (kernel/arch/hosted/zxvd_chat.h), with the feed
// ranked by what each post adds (social_feed.h), never by likes.
import AppKit
import SwiftUI
import ZXVRPC

@MainActor
struct CommonsView: View {
    @EnvironmentObject var store: AppStore
    @State private var draft = ""
    @State private var line: String?
    @State private var replyTo: ChatRecord?
    @State private var open: ChatThread?
    @State private var followId = ""
    @State private var followLine: String?

    private static let quick = ["\u{1F44D}", "\u{2764}\u{FE0F}", "\u{1F4A1}", "\u{1F64F}"]

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            PageHeader(title: "Commons",
                       subtitle: "Signed posts in your own name. The feed ranks by what each post adds, not by likes.")
            if !store.has("chat.feed") {
                NotServed(what: "Commons", method: "chat.feed",
                          detail: "Posts, replies, reactions and follows signed with your own key.")
            } else {
                Text("Your posts stay on this Mac for now: sending them to peers over the mesh is not built yet, and there are no direct messages yet.")
                    .padding(12)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .background(Theme.panel, in: RoundedRectangle(cornerRadius: 10))
                HStack(alignment: .top, spacing: 20) {
                    VStack(alignment: .leading, spacing: 20) {
                        composer
                        feed
                    }
                    .frame(maxWidth: .infinity)
                    VStack(alignment: .leading, spacing: 20) {
                        identity
                        if let t = open { thread(t) }
                    }
                    .frame(width: 380)
                }
            }
        }
        .task { await store.refreshChat() }
    }

    private var composer: some View {
        Panel(title: replyTo == nil ? "New post" : "Reply",
              trailing: "\(draft.utf8.count) / 1024 bytes") {
            if let r = replyTo {
                HStack {
                    Text("Replying to: \(r.text)").lineLimit(1).foregroundStyle(Theme.muted)
                    Spacer()
                    Button("Cancel") { replyTo = nil }
                }
            }
            TextEditor(text: $draft)
                .font(.body)
                .frame(minHeight: 70, maxHeight: 140)
                .scrollContentBackground(.hidden)
                .padding(6)
                .background(Theme.inset, in: RoundedRectangle(cornerRadius: 8))
            HStack {
                if let l = line { Text(l).font(.caption).foregroundStyle(Theme.warn) }
                Spacer()
                Button(replyTo == nil ? "Post" : "Reply") {
                    Task {
                        line = await store.chatPost(draft, replyTo: replyTo?.id)
                        if line == nil {
                            draft = ""
                            if let r = replyTo, let t = open, t.root.id == r.id {
                                open = await store.chatThread(r.id)
                            }
                            replyTo = nil
                        }
                    }
                }
                .keyboardShortcut(.return, modifiers: .command)
                .disabled(draft.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
            }
        }
    }

    private var feed: some View {
        Panel(title: "Feed", trailing: "\(store.chatFeed.count) posts") {
            if store.chatFeed.isEmpty {
                Text("Nothing here yet. Your first post will appear here.")
                    .foregroundStyle(Theme.muted)
            }
            ForEach(store.chatFeed) { p in
                row(p)
                Divider().overlay(Theme.border)
            }
        }
    }

    private func row(_ p: ChatRecord) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 8) {
                Text(p.mine ? "You" : short(p.author))
                    .font(.system(size: 12, weight: .semibold, design: p.mine ? .default : .monospaced))
                    .foregroundStyle(p.mine ? Theme.gold : Theme.text)
                if let d = p.date {
                    Text(d, style: .relative).font(.caption).foregroundStyle(Theme.muted)
                }
                Spacer()
            }
            Text(p.text).textSelection(.enabled)
            HStack(spacing: 8) {
                ForEach(p.reactions, id: \.self) { r in
                    Text("\(r.emoji) \(r.count)")
                        .font(.caption)
                        .padding(.horizontal, 6).padding(.vertical, 2)
                        .background(r.mine ? Theme.gold.opacity(0.18) : Theme.inset,
                                    in: Capsule())
                }
                Menu("React") {
                    ForEach(Self.quick, id: \.self) { e in
                        Button(e) { Task { await store.chatReact(p.id, e) } }
                    }
                }
                .menuStyle(.button)
                .buttonStyle(.borderless)
                .fixedSize()
                Button("Reply") { replyTo = p }.buttonStyle(.borderless)
                Button(p.replies == 1 ? "1 reply" : "\(p.replies) replies") {
                    Task { open = await store.chatThread(p.id) }
                }
                .buttonStyle(.borderless)
                .disabled(p.replies == 0)
            }
            .font(.caption)
        }
        .padding(.vertical, 4)
    }

    private var identity: some View {
        Panel(title: "Your chat identity") {
            if let me = store.chatMe {
                KV(key: "NodeID", value: short(me.nodeId))
                Button("Copy full NodeID") {
                    NSPasteboard.general.clearContents()
                    NSPasteboard.general.setString(me.nodeId, forType: .string)
                }
                .buttonStyle(.borderless)
                KV(key: "Saved", value: me.saved ? "beside your books" : "this run only")
                KV(key: "Records", value: grouped(me.records))
                Text("Following \(me.following.count)").font(.caption).foregroundStyle(Theme.muted)
                ForEach(me.following, id: \.self) { f in
                    HStack {
                        Text(short(f)).font(.system(.caption, design: .monospaced))
                        Spacer()
                        Button("Unfollow") { Task { followLine = await store.chatFollow(f, false) } }
                            .buttonStyle(.borderless)
                    }
                }
                HStack {
                    TextField("NodeID to follow (64 hex digits)", text: $followId)
                        .textFieldStyle(.roundedBorder)
                    Button("Follow") {
                        Task {
                            followLine = await store.chatFollow(followId, true)
                            if followLine == nil { followId = "" }
                        }
                    }
                }
                if let l = followLine { Text(l).font(.caption).foregroundStyle(Theme.warn) }
            } else {
                Text("Loading").foregroundStyle(Theme.muted)
            }
        }
    }

    private func thread(_ t: ChatThread) -> some View {
        Panel(title: "Thread", trailing: "\(t.replies.count) replies") {
            HStack {
                Spacer()
                Button("Close") { open = nil }.buttonStyle(.borderless)
            }
            row(t.root)
            ForEach(t.replies) { r in
                Divider().overlay(Theme.border)
                row(r).padding(.leading, 14)
            }
        }
    }

    private func short(_ hex: String) -> String {
        hex.count > 16 ? String(hex.prefix(8)) + "\u{2026}" + String(hex.suffix(8)) : hex
    }
}
