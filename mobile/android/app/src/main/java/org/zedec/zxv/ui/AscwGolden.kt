// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.ui

import android.content.Context
import android.icu.util.ULocale
import android.os.Build
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import java.util.Locale

/**
 * ASCW Golden, the app's UI font (assets/fonts/ascw-golden, packed into the
 * APK under fonts/ by the ascwGoldenFonts task). The face for the phone's
 * language comes first (Golden Persian for fa, Golden Japanese for ja, ...),
 * then ASCW Golden Latin II, then the other Golden script faces, then the
 * system's fonts. The four CJK faces (3 to 10 MB each) join only for a CJK
 * language; elsewhere the system's CJK font draws stray Han. Android 8 and 9
 * have no custom fallback chain, so they get the language's face alone.
 */
object AscwGolden {
    private const val DIR = "fonts/"
    private val CJK = setOf(
        "ASCW-Golden-ChineseSimplified.ttf", "ASCW-Golden-ChineseTraditional.ttf",
        "ASCW-Golden-Japanese.ttf", "ASCW-Golden-Korean.ttf",
    )

    @Volatile private var cached: Pair<String, FontFamily>? = null

    fun family(context: Context, locale: Locale = Locale.getDefault()): FontFamily {
        val key = locale.toLanguageTag()
        cached?.let { if (it.first == key) return it.second }
        val fam = try {
            build(context, locale)
        } catch (e: Exception) {
            FontFamily.Default // fonts not packed (a test build): system fonts
        }
        cached = key to fam
        return fam
    }

    /** The face file for a locale, as kernel/src/font/ascw_golden.c chooses it. */
    fun faceFor(locale: Locale): String? {
        var tag = locale.toLanguageTag()
        while (tag.isNotEmpty()) {
            AscwGoldenMap.locales.entries.firstOrNull { it.key.equals(tag, ignoreCase = true) }
                ?.let { return it.value }
            tag = tag.substringBeforeLast('-', "")
        }
        val script = ULocale.addLikelySubtags(ULocale.forLocale(locale)).script
        return AscwGoldenMap.scripts[script]
    }

    private fun build(context: Context, locale: Locale): FontFamily {
        val am = context.assets
        val first = faceFor(locale) ?: AscwGoldenMap.PRIMARY
        if (Build.VERSION.SDK_INT < 29) return FontFamily(Font(DIR + first, am))
        val order = buildList {
            add(first)
            add(AscwGoldenMap.PRIMARY)
            addAll(AscwGoldenMap.scripts.values.filter { it !in CJK || it == first })
        }.distinct().take(android.graphics.Typeface.CustomFallbackBuilder.getMaxCustomFallbackCount()) // includes the first
        fun fam(file: String) = android.graphics.fonts.FontFamily.Builder(
            android.graphics.fonts.Font.Builder(am, DIR + file).build(),
        ).build()
        val b = android.graphics.Typeface.CustomFallbackBuilder(fam(order[0]))
        order.drop(1).forEach { b.addCustomFallback(fam(it)) }
        b.setSystemFallback("sans-serif")
        return FontFamily(androidx.compose.ui.text.font.Typeface(b.build()))
    }
}
