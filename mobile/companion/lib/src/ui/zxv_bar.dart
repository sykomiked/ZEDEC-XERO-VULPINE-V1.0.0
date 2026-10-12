// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The ecosystem's one progress bar, in the ZXV boot style: gold on deep
// green. With a value it fills from the centre out to both edges; without
// one, two mirrored bars run in from the edges to meet in the middle.
import 'package:flutter/material.dart';

import 'theme.dart';

class ZxvBar extends StatefulWidget {
  const ZxvBar({super.key, this.value, this.height = 6});

  /// 0..1, or null while the end is unknown.
  final double? value;
  final double height;

  @override
  State<ZxvBar> createState() => _ZxvBarState();
}

class _ZxvBarState extends State<ZxvBar> with SingleTickerProviderStateMixin {
  late final AnimationController _run = AnimationController(vsync: this, duration: const Duration(milliseconds: 1400));

  @override
  void initState() {
    super.initState();
    _sync();
  }

  @override
  void didUpdateWidget(ZxvBar old) {
    super.didUpdateWidget(old);
    _sync();
  }

  void _sync() {
    if (widget.value == null && !_run.isAnimating) {
      _run.repeat();
    } else if (widget.value != null && _run.isAnimating) {
      _run.stop();
    }
  }

  @override
  void dispose() {
    _run.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final v = widget.value;
    return Semantics(
      label: 'Progress',
      value: v == null ? 'working' : '${(v.clamp(0.0, 1.0) * 100).round()}%',
      child: ClipRRect(
        borderRadius: BorderRadius.circular(widget.height / 2),
        child: SizedBox(
          height: widget.height,
          width: double.infinity,
          child: AnimatedBuilder(
            animation: _run,
            builder: (context, _) => CustomPaint(painter: _BarPainter(v, _run.value)),
          ),
        ),
      ),
    );
  }
}

class _BarPainter extends CustomPainter {
  _BarPainter(this.value, this.t);
  final double? value;
  final double t;

  @override
  void paint(Canvas canvas, Size size) {
    canvas.drawRect(Offset.zero & size, Paint()..color = Zc.barGround);
    final gold = Paint()..color = Zc.barGold;
    final half = size.width / 2;
    final v = value;
    if (v != null) {
      final w = half * v.clamp(0.0, 1.0);
      canvas.drawRect(Rect.fromLTRB(half - w, 0, half + w, size.height), gold);
    } else {
      // mirrored bars run inward; each is a fifth of the width
      final seg = size.width / 5;
      final x = (half + seg) * t - seg;
      canvas.drawRect(Rect.fromLTWH(x, 0, seg, size.height), gold);
      canvas.drawRect(Rect.fromLTWH(size.width - x - seg, 0, seg, size.height), gold);
    }
  }

  @override
  bool shouldRepaint(_BarPainter old) => old.value != value || old.t != t;
}
