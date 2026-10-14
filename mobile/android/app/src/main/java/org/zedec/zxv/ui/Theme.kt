// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
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

/** Every Material text style in ASCW Golden (AscwGolden.kt). */
private fun ascwTypography(f: FontFamily): Typography {
    val t = Typography()
    return t.copy(
        displayLarge = t.displayLarge.copy(fontFamily = f),
        displayMedium = t.displayMedium.copy(fontFamily = f),
        displaySmall = t.displaySmall.copy(fontFamily = f),
        headlineLarge = t.headlineLarge.copy(fontFamily = f),
        headlineMedium = t.headlineMedium.copy(fontFamily = f),
        headlineSmall = t.headlineSmall.copy(fontFamily = f),
        titleLarge = t.titleLarge.copy(fontFamily = f),
        titleMedium = t.titleMedium.copy(fontFamily = f),
        titleSmall = t.titleSmall.copy(fontFamily = f),
        bodyLarge = t.bodyLarge.copy(fontFamily = f),
        bodyMedium = t.bodyMedium.copy(fontFamily = f),
        bodySmall = t.bodySmall.copy(fontFamily = f),
        labelLarge = t.labelLarge.copy(fontFamily = f),
        labelMedium = t.labelMedium.copy(fontFamily = f),
        labelSmall = t.labelSmall.copy(fontFamily = f),
    )
}

@Composable
fun ZxvTheme(content: @Composable () -> Unit) {
    val context = LocalContext.current
    val typography = remember { ascwTypography(AscwGolden.family(context)) }
    MaterialTheme(
        typography = typography,
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
