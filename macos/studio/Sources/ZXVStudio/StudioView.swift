// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Local AI Studio: swarm.ask, model.info, and swarm.state's companion
// budget, answer tag and agents.
import SwiftUI
import ZXVRPC

@MainActor
struct StudioView: View {
    @EnvironmentObject var store: AppStore
    @State private var draft = ""

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            PageHeader(title: "Local AI Studio",
                       subtitle: "Talk to the companion. Every answer is generated on this Mac inside the swarm's token budget.") {
                ModelChip(model: store.state?.model)
            }
            HStack(alignment: .top, spacing: 20) {
                conversation.frame(minWidth: 420)
                VStack(spacing: 16) {
                    budget
                    modelSlot
                    lastCheck
                }
                .frame(width: 330)
            }
            SwarmTable(state: store.state)
        }
    }

    private var conversation: some View {
        Panel(title: "Companion", trailing: "Answers stay on this Mac") {
            ScrollViewReader { proxy in
                ScrollView {
                    VStack(alignment: .leading, spacing: 12) {
                        if store.chat.isEmpty {
                            Text(store.state?.model.canGenerate == true
                                 ? "Ask anything. The companion answers within this cycle's budget."
                                 : "No model is installed, so the companion reads Hackronomicon shorthand back instead of answering. Put a .gguf model in ~/Library/Application Support/ZXV/models.")
                                .foregroundStyle(Theme.muted)
                        }
                        ForEach(store.chat) { line in
                            ChatBubble(line: line).id(line.id)
                        }
                        if store.asking {
                            ProgressView().controlSize(.small).padding(.leading, 4)
                        }
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                }
                .frame(minHeight: 380)
                .onChange(of: store.chat.count) { _ in
                    if let last = store.chat.last { withAnimation { proxy.scrollTo(last.id, anchor: .bottom) } }
                }
            }
            HStack(spacing: 10) {
                TextField("Ask, or write shorthand like fan_out(question) -> verify(answers)", text: $draft)
                    .textFieldStyle(.roundedBorder)
                    .onSubmit(send)
                    .disabled(store.asking)
                Button("Send", action: send)
                    .buttonStyle(.borderedProminent)
                    .keyboardShortcut(.return, modifiers: [.command])
                    .disabled(store.asking || draft.trimmingCharacters(in: .whitespaces).isEmpty)
            }
        }
    }

    private func send() {
        let p = draft
        draft = ""
        Task { await store.ask(p) }
    }

    private var budget: some View {
        let c = store.state?.companion
        let allotted = max(c?.allotted ?? 0, 1)
        let remaining = c?.remaining ?? 0
        return Panel(title: "This cycle's budget") {
            HStack(alignment: .firstTextBaseline, spacing: 8) {
                Text(grouped(remaining)).font(.system(size: 28, design: .monospaced))
                Text("of \(grouped(c?.allotted ?? 0)) tokens left").foregroundStyle(Theme.muted)
            }
            ProgressView(value: Double(remaining), total: Double(allotted)).tint(Theme.gold)
            KV(key: "Used", value: grouped(c?.used ?? 0))
            KV(key: "Max per answer", value: grouped(c?.maxPerAnswer ?? 0))
            KV(key: "Asks", value: grouped(c?.asks ?? 0))
            KV(key: "Turned away, budget spent", value: grouped(c?.exhaustedAsks ?? 0))
            if c?.exhausted == true {
                Text("Budget spent for this cycle. The next cycle opens a fresh allotment.")
                    .font(Theme.callout).foregroundStyle(Theme.gold)
            }
        }
    }

    private var modelSlot: some View {
        let m = store.state?.model
        return Panel(title: "Model slot", trailing: m?.canGenerate == true ? "can generate" : "cannot generate") {
            Text(m?.loaded == true ? m!.name : "No model loaded").fontWeight(.semibold)
            if let m, m.loaded {
                KV(key: "Architecture", value: m.arch)
                KV(key: "Layers", value: grouped(m.layers))
                KV(key: "Vocabulary", value: grouped(m.vocab))
                KV(key: "Context", value: grouped(m.context))
            }
            Text(m?.status ?? "").font(Theme.caption).foregroundStyle(Theme.muted)
        }
    }

    private var lastCheck: some View {
        let c = store.state?.companion
        return Panel(title: "Last answer check") {
            if let t = c?.lastTag {
                KV(key: "Truth", value: t.base)
                KV(key: "Polarity", value: t.polarity)
                KV(key: "Phase", value: t.phase)
                KV(key: "Action", value: t.action, color: t.action == "proceed" ? Theme.good : Theme.gold)
            } else {
                Text("No model answer yet.").foregroundStyle(Theme.muted)
            }
            KV(key: "Quarantined answers",
               value: "\(c?.quarantined ?? 0), log \(c?.quarantineOk == false ? "FAILED" : "verified")",
               color: c?.quarantineOk == false ? Theme.warn : Theme.text)
        }
    }
}

@MainActor
struct ModelChip: View {
    let model: StateModel?

    var body: some View {
        HStack(spacing: 8) {
            Circle().fill(model?.canGenerate == true ? Theme.good : Theme.muted).frame(width: 8, height: 8)
            Text(model?.loaded == true ? "\(model!.name) ready" : "No model")
        }
        .font(Theme.callout)
        .padding(.horizontal, 14)
        .padding(.vertical, 8)
        .overlay(Capsule().strokeBorder(Theme.border))
    }
}

@MainActor
struct ChatBubble: View {
    let line: ChatLine

    var body: some View {
        HStack {
            if line.mine { Spacer(minLength: 60) }
            VStack(alignment: .leading, spacing: 6) {
                Text(line.mine ? "You" : "Companion").font(Theme.caption).foregroundStyle(Theme.muted)
                Text(line.text).textSelection(.enabled).lineSpacing(3)
                if let t = line.tag {
                    HStack(spacing: 6) {
                        TagChip(text: t.base)
                        TagChip(text: t.polarity)
                        TagChip(text: t.phase)
                        TagChip(text: t.action, color: t.action == "proceed" ? Theme.good : Theme.gold)
                        if let n = line.charged {
                            Text("\(n) tokens").font(.system(size: 11, design: .monospaced)).foregroundStyle(Theme.muted)
                        }
                    }
                }
            }
            .padding(12)
            .background(line.mine ? Theme.border : Theme.inset, in: RoundedRectangle(cornerRadius: 12))
            .overlay(RoundedRectangle(cornerRadius: 12).strokeBorder(line.mine ? Color.clear : Theme.border))
            if !line.mine { Spacer(minLength: 60) }
        }
    }
}

@MainActor
struct TagChip: View {
    let text: String
    var color: Color = Theme.text.opacity(0.85)

    var body: some View {
        Text(text)
            .font(.system(size: 11, design: .monospaced))
            .foregroundStyle(color)
            .padding(.horizontal, 7)
            .padding(.vertical, 3)
            .overlay(RoundedRectangle(cornerRadius: 6).strokeBorder(Theme.border))
    }
}

@MainActor
struct SwarmTable: View {
    let state: SwarmState?

    var body: some View {
        Panel(title: "Swarm, this cycle",
              trailing: state.map { "Levels \($0.levels) · Agents \($0.agents.count) · Tokens per cycle \(grouped($0.tokensPerCycle)) · Cycle \($0.cycle) · Mood \($0.mood)" }) {
            Grid(alignment: .leading, horizontalSpacing: 16, verticalSpacing: 8) {
                GridRow {
                    Text("Level"); Text("Agent"); Text("Feeling")
                    Text("Tokens").gridColumnAlignment(.trailing)
                    Text("Split"); Text("Witness")
                }
                .font(Theme.caption).foregroundStyle(Theme.muted)
                Divider().gridCellUnsizedAxes(.horizontal)
                ForEach(state?.agents ?? []) { a in
                    GridRow {
                        Text("\(a.level)").font(.system(.body, design: .monospaced))
                        Text(a.name)
                        Text("\(a.emoji) \(a.intensity)")
                        Text(grouped(a.tokens)).font(.system(.body, design: .monospaced))
                        SplitBar(agent: a).frame(minWidth: 160, maxWidth: .infinity)
                        Text(a.witness == 1 ? "true, by \(a.witnessBy)" : "not verified, by \(a.witnessBy)")
                            .foregroundStyle(a.witness == 1 ? Theme.good : Theme.warn)
                    }
                    .font(Theme.callout)
                }
            }
            HStack(spacing: 18) {
                Legend(color: Theme.floor, text: "Fibonacci floor")
                Legend(color: Theme.gold, text: "Market")
                Legend(color: Theme.emotional, text: "Emotional (imaginary)")
            }
            .font(Theme.caption)
        }
    }
}

@MainActor
struct SplitBar: View {
    let agent: Agent

    var body: some View {
        GeometryReader { g in
            let total = max(Double(agent.tokens), 1)
            HStack(spacing: 0) {
                Theme.floor.frame(width: g.size.width * Double(agent.re) / total)
                Theme.gold.frame(width: g.size.width * Double(agent.mk) / total)
                Theme.emotional.frame(width: g.size.width * Double(agent.im) / total)
            }
        }
        .frame(height: 8)
        .background(Theme.border)
        .clipShape(RoundedRectangle(cornerRadius: 4))
        .accessibilityLabel("Floor \(agent.re), market \(agent.mk), emotional \(agent.im) tokens")
    }
}

@MainActor
struct Legend: View {
    let color: Color
    let text: String

    var body: some View {
        HStack(spacing: 6) {
            RoundedRectangle(cornerRadius: 2).fill(color).frame(width: 10, height: 10)
            Text(text).foregroundStyle(Theme.muted)
        }
    }
}
