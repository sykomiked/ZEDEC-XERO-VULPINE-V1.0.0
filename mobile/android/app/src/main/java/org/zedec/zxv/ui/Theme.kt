// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily

/** Colours of the desktop mockups (the .dc.html files in zxvui/project). */
object Zc {
    val Bg = Color(0xFF0E1116)
    val Surface = Color(0xFF161B22)
    val Raised = Color(0xFF1F2630)
    val Border = Color(0xFF2A313C)
    val Text = Color(0xFFE6EDF3)
    val Muted = Color(0xFF9AA4AE)
    val Gold = Color(0xFFD4A72C)
    val GoldSoft = Color(0xFFE8C25A)
    val Green = Color(0xFF3FB950)
    val Blue = Color(0xFF4C8DFF)
    val Mono = FontFamily.Monospace
}

@Composable
fun ZxvTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = darkColorScheme(
            primary = Zc.Gold,
            onPrimary = Zc.Bg,
            secondary = Zc.Blue,
            background = Zc.Bg,
            onBackground = Zc.Text,
            surface = Zc.Surface,
            onSurface = Zc.Text,
            surfaceVariant = Zc.Raised,
            onSurfaceVariant = Zc.Muted,
            outline = Zc.Border,
        ),
        content = content,
    )
}
