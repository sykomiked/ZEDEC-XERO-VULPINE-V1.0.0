// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Shared pieces of the five views: panels, label/value rows, counters, and
// the dashed "not served yet" box for panels waiting on a zxvd method.
import 'package:flutter/material.dart';

import 'theme.dart';

class PageHeader extends StatelessWidget {
  const PageHeader({super.key, required this.title, required this.subtitle, this.trailing});
  final String title, subtitle;
  final Widget? trailing;

  @override
  Widget build(BuildContext context) => Padding(
        padding: const EdgeInsets.only(bottom: 16),
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Row(children: [
            Expanded(
                child: Text(title,
                    style: const TextStyle(fontSize: 22, fontWeight: FontWeight.w600, color: Zc.text))),
            ?trailing,
          ]),
          const SizedBox(height: 4),
          Text(subtitle, style: const TextStyle(color: Zc.muted)),
        ]),
      );
}

class Panel extends StatelessWidget {
  const Panel({super.key, required this.title, this.trailing, required this.children, this.dashed = false});
  final String title;
  final String? trailing;
  final List<Widget> children;
  final bool dashed;

  @override
  Widget build(BuildContext context) {
    final body = Padding(
      padding: const EdgeInsets.all(16),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Row(children: [
          Expanded(
              child: Text(title.toUpperCase(),
                  style: const TextStyle(
                      fontSize: 12, fontWeight: FontWeight.w600, letterSpacing: 1, color: Zc.muted))),
          if (trailing != null) Flexible(child: Text(trailing!, style: const TextStyle(fontSize: 12, color: Zc.muted))),
        ]),
        const SizedBox(height: 12),
        ...children,
      ]),
    );
    return Container(
      margin: const EdgeInsets.only(bottom: 14),
      width: double.infinity,
      decoration: dashed
          ? null
          : BoxDecoration(
              color: Zc.panel,
              borderRadius: BorderRadius.circular(14),
              border: Border.all(color: Zc.border),
            ),
      child: dashed ? CustomPaint(painter: _Dashed(), child: body) : body,
    );
  }
}

class _Dashed extends CustomPainter {
  @override
  void paint(Canvas canvas, Size size) {
    final p = Paint()
      ..color = Zc.muted.withValues(alpha: 0.5)
      ..style = PaintingStyle.stroke;
    final path = Path()..addRRect(RRect.fromRectAndRadius(Offset.zero & size, const Radius.circular(14)));
    for (final m in path.computeMetrics()) {
      for (var d = 0.0; d < m.length; d += 9) {
        canvas.drawPath(m.extractPath(d, d + 5), p);
      }
    }
  }

  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) => false;
}

/// A label and a monospaced value on one row.
class KV extends StatelessWidget {
  const KV(this.k, this.v, {super.key, this.color = Zc.text});
  final String k, v;
  final Color color;

  @override
  Widget build(BuildContext context) => Padding(
        padding: const EdgeInsets.symmetric(vertical: 3),
        child: Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Flexible(child: Text(k, style: const TextStyle(color: Zc.muted))),
          const SizedBox(width: 12),
          Expanded(
              child: SelectableText(v, textAlign: TextAlign.right, style: Zc.mono.copyWith(color: color, fontSize: 13))),
        ]),
      );
}

/// A big number with a caption.
class Stat extends StatelessWidget {
  const Stat(this.value, this.label, {super.key});
  final String value, label;

  @override
  Widget build(BuildContext context) => Container(
        padding: const EdgeInsets.all(12),
        decoration: BoxDecoration(
            color: Zc.inset, borderRadius: BorderRadius.circular(12), border: Border.all(color: Zc.border)),
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          FittedBox(child: Text(value, style: Zc.mono.copyWith(fontSize: 20, color: Zc.text))),
          const SizedBox(height: 4),
          Text(label, style: const TextStyle(fontSize: 12, color: Zc.muted)),
        ]),
      );
}

class StatGrid extends StatelessWidget {
  const StatGrid(this.stats, {super.key});
  final List<Stat> stats;

  @override
  Widget build(BuildContext context) => LayoutBuilder(builder: (context, c) {
        final cols = c.maxWidth > 520 ? 3 : 2;
        final w = (c.maxWidth - 10 * (cols - 1)) / cols;
        return Wrap(spacing: 10, runSpacing: 10, children: [for (final s in stats) SizedBox(width: w, child: s)]);
      });
}

/// A panel waiting on a zxvd method that is not served yet.
class NotServed extends StatelessWidget {
  const NotServed({super.key, required this.what, required this.method, required this.detail});
  final String what, method, detail;

  @override
  Widget build(BuildContext context) => Panel(title: what, trailing: 'not served yet', dashed: true, children: [
        Text(detail, style: const TextStyle(color: Zc.muted)),
        const SizedBox(height: 6),
        Text('Lights up when zxvd serves $method.', style: Zc.mono.copyWith(fontSize: 11, color: Zc.muted)),
      ]);
}

class Legend extends StatelessWidget {
  const Legend(this.color, this.text, {super.key});
  final Color color;
  final String text;

  @override
  Widget build(BuildContext context) => Row(mainAxisSize: MainAxisSize.min, children: [
        Container(width: 10, height: 10, decoration: BoxDecoration(color: color, borderRadius: BorderRadius.circular(2))),
        const SizedBox(width: 6),
        Text(text, style: const TextStyle(fontSize: 12, color: Zc.muted)),
      ]);
}

class Chip2 extends StatelessWidget {
  const Chip2(this.text, {super.key, this.color = Zc.text});
  final String text;
  final Color color;

  @override
  Widget build(BuildContext context) => Container(
        padding: const EdgeInsets.symmetric(horizontal: 7, vertical: 3),
        decoration: BoxDecoration(borderRadius: BorderRadius.circular(6), border: Border.all(color: Zc.border)),
        child: Text(text, style: Zc.mono.copyWith(fontSize: 11, color: color)),
      );
}

/// A line under a form: muted when fine, warm when refused.
class FormLine extends StatelessWidget {
  const FormLine(this.ok, this.text, {super.key});
  final bool ok;
  final String text;

  @override
  Widget build(BuildContext context) => Padding(
        padding: const EdgeInsets.only(top: 8),
        child: Text(text, style: TextStyle(fontSize: 13, color: ok ? Zc.muted : Zc.warn)),
      );
}

class Labeled extends StatelessWidget {
  const Labeled(this.label, this.child, {super.key});
  final String label;
  final Widget child;

  @override
  Widget build(BuildContext context) => Padding(
        padding: const EdgeInsets.only(bottom: 10),
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Text(label, style: const TextStyle(fontSize: 12, color: Zc.muted)),
          const SizedBox(height: 4),
          child,
        ]),
      );
}
