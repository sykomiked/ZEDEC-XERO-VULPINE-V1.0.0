// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import org.zedec.zxv.model.AppState
import org.zedec.zxv.model.ChatMessage

@Composable
fun AssistantScreen(state: AppState) {
    var input by remember { mutableStateOf("") }
    Column(Modifier.fillMaxSize()) {
        ScreenTitle("Assistant")
        Muted(state.routeLabel())
        LazyColumn(
            Modifier.weight(1f).fillMaxWidth().padding(vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            itemsIndexed(state.messages) { _, m -> Bubble(m) }
        }
        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(bottom = 8.dp)) {
            OutlinedTextField(
                value = input,
                onValueChange = { input = it },
                placeholder = { Text("Ask the assistant") },
                modifier = Modifier.weight(1f),
            )
            Spacer(Modifier.width(8.dp))
            Button(onClick = { state.send(input); input = "" }) { Text("Send") }
        }
    }
}

@Composable
private fun Bubble(m: ChatMessage) {
    Column(
        Modifier.fillMaxWidth().padding(start = if (m.fromUser) 48.dp else 0.dp, end = if (m.fromUser) 0.dp else 48.dp)
            .background(if (m.fromUser) Zc.Raised else Zc.Surface, RoundedCornerShape(12.dp))
            .padding(12.dp),
    ) {
        if (!m.fromUser && m.where.isNotEmpty()) Muted(m.where)
        Text(if (m.text.isEmpty()) "…" else m.text, color = Zc.Text)
    }
}
