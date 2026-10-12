// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The look of the approved mockups and the Mac app (macos/studio Theme.swift):
// dark ground, gold accent, panels with a thin border.
import 'package:flutter/material.dart';

class Zc {
  static const ground = Color(0xFF0C0F14);
  static const sidebar = Color(0xFF10141B);
  static const panel = Color(0xFF141922);
  static const inset = Color(0xFF0F1319);
  static const border = Color(0xFF222A35);
  static const text = Color(0xFFE7E9EE);
  static const muted = Color(0xFF9AA3B2);
  static const gold = Color(0xFFD9A92E);
  static const good = Color(0xFF8FD3A8);
  static const warn = Color(0xFFF0A35E);
  static const floor = Color(0xFF4C8DFF);
  static const emotional = Color(0xFFE05FA8);
  // progress bars: gold on deep green (zxv_bar.dart for connecting)
  static const barGold = Color(0xFFF2C230);
  static const barGround = Color(0xFF002911);

  static const mono = TextStyle(fontFamily: 'monospace', fontFamilyFallback: ['Menlo', 'Courier']);
}

ThemeData zxvTheme() {
  final base = ThemeData(
    brightness: Brightness.dark,
    useMaterial3: true,
    colorScheme: const ColorScheme.dark(
      primary: Zc.gold,
      onPrimary: Color(0xFF1A1405),
      secondary: Zc.gold,
      surface: Zc.panel,
      onSurface: Zc.text,
      error: Zc.warn,
    ),
    scaffoldBackgroundColor: Zc.ground,
  );
  return base.copyWith(
    appBarTheme: const AppBarTheme(backgroundColor: Zc.ground, foregroundColor: Zc.text, elevation: 0),
    navigationBarTheme: NavigationBarThemeData(
      backgroundColor: Zc.sidebar,
      indicatorColor: Zc.gold.withValues(alpha: 0.22),
      labelTextStyle: WidgetStateProperty.all(const TextStyle(fontSize: 11, color: Zc.text)),
    ),
    inputDecorationTheme: const InputDecorationTheme(
      isDense: true,
      border: OutlineInputBorder(borderSide: BorderSide(color: Zc.border)),
      enabledBorder: OutlineInputBorder(borderSide: BorderSide(color: Zc.border)),
      hintStyle: TextStyle(color: Zc.muted),
      labelStyle: TextStyle(color: Zc.muted),
    ),
    dividerColor: Zc.border,
  );
}
