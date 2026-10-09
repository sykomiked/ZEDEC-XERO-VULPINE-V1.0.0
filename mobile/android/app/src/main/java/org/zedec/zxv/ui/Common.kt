// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

@Composable
fun Panel(title: String? = null, modifier: Modifier = Modifier, content: @Composable ColumnScope.() -> Unit) {
    Card(
        modifier = modifier.fillMaxWidth().padding(vertical = 6.dp),
        shape = RoundedCornerShape(12.dp),
        colors = CardDefaults.cardColors(containerColor = Zc.Surface),
        border = BorderStroke(1.dp, Zc.Border),
    ) {
        Column(Modifier.padding(16.dp)) {
            if (title != null) {
                Text(title, fontSize = 16.sp, fontWeight = FontWeight.SemiBold, color = Zc.Text)
            }
            content()
        }
    }
}

@Composable
fun Muted(text: String, modifier: Modifier = Modifier) {
    Text(text, color = Zc.Muted, fontSize = 13.sp, modifier = modifier)
}

@Composable
fun MonoText(text: String, modifier: Modifier = Modifier) {
    Text(text, fontFamily = Zc.Mono, color = Zc.GoldSoft, fontSize = 14.sp, modifier = modifier)
}

@Composable
fun ScreenTitle(text: String) {
    Text(text, fontSize = 24.sp, fontWeight = FontWeight.SemiBold, color = Zc.Text,
        modifier = Modifier.padding(top = 8.dp, bottom = 4.dp))
}
