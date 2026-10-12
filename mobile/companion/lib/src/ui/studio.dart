// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Local AI Studio: swarm.ask, and swarm.state's companion budget, model slot,
// answer tag and agents (as macos/studio StudioView.swift).
import 'package:flutter/material.dart';

import '../json.dart';
import '../money.dart';
import '../store.dart';
import 'theme.dart';
import 'widgets.dart';

class StudioView extends StatefulWidget {
  const StudioView({super.key, required this.store});
  final AppStore store;

  @override
  State<StudioView> createState() => _StudioViewState();
}

class _StudioViewState extends State<StudioView> {
  final _draft = TextEditingController();

  @override
  void dispose() {
    _draft.dispose();
    super.dispose();
  }

  void _send() {
    final p = _draft.text;
    if (p.trim().isEmpty) return;
    _draft.clear();
    widget.store.ask(p);
  }

  @override
  Widget build(BuildContext context) {
    final s = widget.store;
    final model = s.state['model'];
    return ListView(padding: const EdgeInsets.all(16), children: [
      PageHeader(
        title: 'Local AI Studio',
        subtitle: 'Talk to the companion. Answers are generated on your Mac, inside the swarm\'s token budget.',
        trailing: _ModelChip(model),
      ),
      Panel(title: 'Companion', trailing: 'Answers stay on your devices', children: [
        if (s.chat.isEmpty)
          Text(
              model.boolean('can_generate')
                  ? 'Ask anything. The companion answers within this cycle\'s budget.'
                  : 'No model is installed on the Mac, so the companion reads shorthand back instead of answering.',
              style: const TextStyle(color: Zc.muted)),
        for (final l in s.chat) _Bubble(l),
        if (s.asking) const Padding(padding: EdgeInsets.all(6), child: LinearProgressIndicator(color: Zc.barGold, backgroundColor: Zc.barGround)),
        const SizedBox(height: 8),
        Row(children: [
          Expanded(
            child: TextField(
              key: const Key('ask'),
              controller: _draft,
              enabled: !s.asking,
              minLines: 1,
              maxLines: 4,
              textInputAction: TextInputAction.send,
              onSubmitted: (_) => _send(),
              decoration: const InputDecoration(hintText: 'Ask, or write shorthand like fan_out(q) -> verify(a)'),
            ),
          ),
          const SizedBox(width: 8),
          FilledButton(key: const Key('send'), onPressed: s.asking ? null : _send, child: const Text('Send')),
        ]),
      ]),
      _budget(s.state['companion']),
      _modelSlot(model),
      _lastCheck(s.state['companion']),
      _SwarmList(s.state),
    ]);
  }

  Widget _budget(J c) {
    final allotted = c.integer('allotted');
    final remaining = c.integer('remaining');
    return Panel(title: 'This cycle\'s budget', children: [
      Row(crossAxisAlignment: CrossAxisAlignment.baseline, textBaseline: TextBaseline.alphabetic, children: [
        Text(grouped(remaining), style: Zc.mono.copyWith(fontSize: 26, color: Zc.text)),
        const SizedBox(width: 8),
        Flexible(child: Text('of ${grouped(allotted)} tokens left', style: const TextStyle(color: Zc.muted))),
      ]),
      const SizedBox(height: 6),
      LinearProgressIndicator(
          value: allotted <= 0 ? 0 : remaining / allotted, color: Zc.barGold, backgroundColor: Zc.barGround),
      const SizedBox(height: 8),
      KV('Used', grouped(c.integer('used'))),
      KV('Max per answer', grouped(c.integer('max_per_answer'))),
      KV('Asks', grouped(c.integer('asks'))),
      KV('Turned away, budget spent', grouped(c.integer('exhausted_asks'))),
      if (c.boolean('exhausted'))
        const Text('Budget spent for this cycle. The next cycle opens a fresh allotment.',
            style: TextStyle(color: Zc.gold)),
    ]);
  }

  Widget _modelSlot(J m) => Panel(
        title: 'Model slot',
        trailing: m.boolean('can_generate') ? 'can generate' : 'cannot generate',
        children: [
          Text(m.boolean('loaded') ? m.str('name') : 'No model loaded',
              style: const TextStyle(fontWeight: FontWeight.w600)),
          if (m.boolean('loaded')) ...[
            KV('Architecture', m.str('arch')),
            KV('Layers', grouped(m.integer('layers'))),
            KV('Vocabulary', grouped(m.integer('vocab'))),
            KV('Context', grouped(m.integer('context'))),
          ],
          const SizedBox(height: 4),
          Text(m.str('status'), style: const TextStyle(fontSize: 12, color: Zc.muted)),
        ],
      );

  Widget _lastCheck(J c) {
    final t = c['last_tag'];
    return Panel(title: 'Last answer check', children: [
      if (!t.isNull) ...[
        KV('Truth', t.str('base')),
        KV('Polarity', t.str('polarity')),
        KV('Phase', t.str('phase')),
        KV('Action', t.str('action'), color: t.str('action') == 'proceed' ? Zc.good : Zc.gold),
      ] else
        const Text('No model answer yet.', style: TextStyle(color: Zc.muted)),
      KV('Quarantined answers',
          '${c.integer('quarantined')}, log ${c.boolean('quarantine_ok', true) ? 'verified' : 'FAILED'}',
          color: c.boolean('quarantine_ok', true) ? Zc.text : Zc.warn),
    ]);
  }
}

class _ModelChip extends StatelessWidget {
  const _ModelChip(this.m);
  final J m;

  @override
  Widget build(BuildContext context) => Container(
        padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
        decoration: BoxDecoration(borderRadius: BorderRadius.circular(20), border: Border.all(color: Zc.border)),
        child: Row(mainAxisSize: MainAxisSize.min, children: [
          Container(
              width: 8,
              height: 8,
              decoration: BoxDecoration(
                  shape: BoxShape.circle, color: m.boolean('can_generate') ? Zc.good : Zc.muted)),
          const SizedBox(width: 6),
          Text(m.boolean('loaded') ? 'ready' : 'No model', style: const TextStyle(fontSize: 12)),
        ]),
      );
}

class _Bubble extends StatelessWidget {
  const _Bubble(this.l);
  final ChatLine l;

  @override
  Widget build(BuildContext context) {
    final t = l.tag;
    return Align(
      alignment: l.mine ? Alignment.centerRight : Alignment.centerLeft,
      child: Container(
        margin: EdgeInsets.only(bottom: 10, left: l.mine ? 40 : 0, right: l.mine ? 0 : 40),
        padding: const EdgeInsets.all(12),
        decoration: BoxDecoration(
          color: l.mine ? Zc.border : Zc.inset,
          borderRadius: BorderRadius.circular(12),
          border: Border.all(color: l.mine ? Colors.transparent : Zc.border),
        ),
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Text(l.mine ? 'You' : 'Companion', style: const TextStyle(fontSize: 11, color: Zc.muted)),
          const SizedBox(height: 4),
          SelectableText(l.text),
          if (t != null) ...[
            const SizedBox(height: 6),
            Wrap(spacing: 6, runSpacing: 4, children: [
              Chip2(t.str('base')),
              Chip2(t.str('polarity')),
              Chip2(t.str('phase')),
              Chip2(t.str('action'), color: t.str('action') == 'proceed' ? Zc.good : Zc.gold),
              if (l.charged != null) Text('${l.charged} tokens', style: Zc.mono.copyWith(fontSize: 11, color: Zc.muted)),
            ]),
          ],
        ]),
      ),
    );
  }
}

class _SwarmList extends StatelessWidget {
  const _SwarmList(this.s);
  final J s;

  @override
  Widget build(BuildContext context) {
    final agents = s.list('agents');
    return Panel(
      title: 'Swarm, this cycle',
      trailing: s.isNull
          ? null
          : 'Levels ${s.integer('levels')} · ${agents.length} agents · ${grouped(s.integer('tokens_per_cycle'))} tokens · mood ${s.str('mood')}',
      children: [
        for (final a in agents)
          Padding(
            padding: const EdgeInsets.symmetric(vertical: 6),
            child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
              Row(children: [
                Text('L${a.integer('level')}', style: Zc.mono.copyWith(color: Zc.muted, fontSize: 12)),
                const SizedBox(width: 8),
                Expanded(child: Text(a.str('name'))),
                Text('${a.str('emoji')} ${a.integer('intensity')}'),
                const SizedBox(width: 10),
                Text(grouped(a.integer('re') + a.integer('mk') + a.integer('im')),
                    style: Zc.mono.copyWith(fontSize: 13)),
              ]),
              const SizedBox(height: 4),
              _SplitBar(a),
              const SizedBox(height: 2),
              Text(
                  a.integer('witness') == 1
                      ? 'true, by ${a.str('witness_by')}'
                      : 'not verified, by ${a.str('witness_by')}',
                  style: TextStyle(fontSize: 11, color: a.integer('witness') == 1 ? Zc.good : Zc.warn)),
            ]),
          ),
        const SizedBox(height: 6),
        const Wrap(spacing: 14, runSpacing: 4, children: [
          Legend(Zc.floor, 'Fibonacci floor'),
          Legend(Zc.gold, 'Market'),
          Legend(Zc.emotional, 'Emotional (imaginary)'),
        ]),
      ],
    );
  }
}

class _SplitBar extends StatelessWidget {
  const _SplitBar(this.a);
  final J a;

  @override
  Widget build(BuildContext context) {
    final re = a.integer('re'), mk = a.integer('mk'), im = a.integer('im');
    final total = re + mk + im;
    return Semantics(
      label: 'Floor $re, market $mk, emotional $im tokens',
      child: ClipRRect(
        borderRadius: BorderRadius.circular(4),
        child: SizedBox(
          height: 8,
          child: total == 0
              ? Container(color: Zc.border)
              : Row(children: [
                  if (re > 0) Expanded(flex: re, child: Container(color: Zc.floor)),
                  if (mk > 0) Expanded(flex: mk, child: Container(color: Zc.gold)),
                  if (im > 0) Expanded(flex: im, child: Container(color: Zc.emotional)),
                ]),
        ),
      ),
    );
  }
}
