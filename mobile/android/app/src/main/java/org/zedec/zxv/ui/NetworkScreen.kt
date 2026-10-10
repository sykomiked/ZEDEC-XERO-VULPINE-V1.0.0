// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Button
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.google.zxing.BarcodeFormat
import com.google.zxing.qrcode.QRCodeWriter
import org.zedec.zxv.model.AppState
import org.zedec.zxv.model.Device
import org.zedec.zxv.model.Zxv

@Composable
fun NetworkScreen(state: AppState) {
    var code by remember { mutableStateOf("") }
    LazyColumn {
        item {
            ScreenTitle("Network and devices")
            Row {
                listOf(Zxv.NET_OFFLINE to "Offline", Zxv.NET_LAN to "Local network", Zxv.NET_UNMETERED to "Online")
                    .forEach { (mode, label) ->
                        val on = state.netMode == mode
                        OutlinedButton(onClick = { state.setNetwork(mode, 8192, 400, 80, false) }) {
                            Text(if (on) "• $label" else label, fontSize = 13.sp)
                        }
                        Spacer(Modifier.width(6.dp))
                    }
            }
            Muted(
                when (state.netMode) {
                    Zxv.NET_OFFLINE -> "Everything runs on this phone. Actions queue and sync later."
                    Zxv.NET_LAN -> "Nearby devices only. No internet needed."
                    else -> "Local network plus the wider mesh. No server in the middle."
                }
            )
            if (state.notice.isNotEmpty()) Text(state.notice, color = Zc.GoldSoft, modifier = Modifier)
        }
        item { Panel(title = "Your devices") { Muted("Signed roster. Revoked devices cannot open new sessions.") } }
        items(state.devices) { d -> DeviceRow(state, d) }
        item {
            Panel(title = "Pair a device") {
                Row {
                    Button(onClick = { state.startInvite(forHome = true) }) { Text("Add home computer") }
                    Spacer(Modifier.width(8.dp))
                    OutlinedButton(onClick = { state.startInvite(forHome = false) }) { Text("Add phone") }
                }
                val inv = state.invitation
                if (inv != null) {
                    Spacer(Modifier.size(8.dp))
                    Muted("Scan this on the new device, or type the code there:")
                    QrCode(inv.qrText)
                    Text(inv.code.chunked(5).joinToString("-"), fontFamily = Zc.Mono, fontSize = 26.sp, color = Zc.GoldSoft)
                    Muted("Valid for 10 minutes. Both screens will then show the same 6-digit number.")
                }
                Spacer(Modifier.size(10.dp))
                Muted("Joining from this phone? Type the code shown on your other device:")
                OutlinedTextField(value = code, onValueChange = { code = it }, placeholder = { Text("XXXXX-XXXXX") })
                Row {
                    TextButton(onClick = { state.joinWithCode(code) }) { Text("Join with code") }
                    TextButton(onClick = { state.joinWithQr(code) }) { Text("Join with pasted QR text") }
                }
            }
        }
        item {
            Panel(title = "What this phone can run") {
                if (state.localModels.isEmpty()) Muted("No model fits this phone's memory budget; prompts go to your home node.")
                state.localModels.forEach { MonoText(it) }
                Muted("Large models run on the home node. When it is unreachable, prompts fall back to the small model here.")
            }
        }
        item {
            Panel(title = "Buy extra capacity") {
                Muted("Providers on the network offer compute, model inference, storage and bandwidth at an asking fee of their choosing. " +
                    "Each round a uniform-price auction finds the rate where supply meets demand; everyone pays the same rate, never more than they bid. " +
                    "No provider may take more than 8/21 of a round when others can serve. Money stays in escrow and is paid only for service delivered.")
                Muted("Market ordering is not bridged to the phone UI in this build (kernel/src/capmkt is in the core).")
            }
        }
    }
}

@Composable
private fun DeviceRow(state: AppState, d: Device) {
    var editing by remember { mutableStateOf(false) }
    var name by remember { mutableStateOf(d.name) }
    Panel {
        val role = when (d.role) { Zxv.ROLE_HOME -> "Home node"; Zxv.ROLE_THIN -> "Phone (thin client)"; else -> "Phone (standalone)" }
        val tags = buildList {
            if (d.flags and Zxv.FLAG_ADMIN != 0) add("admin")
            if (d.flags and Zxv.FLAG_HELD != 0) add("confirms payments")
            if (d.revoked) add("revoked")
        }.joinToString(" · ")
        Text(d.name, fontWeight = FontWeight.SemiBold, color = if (d.revoked) Zc.Muted else Zc.Text)
        Muted("$role${if (tags.isEmpty()) "" else " · $tags"}")
        MonoText(d.idHex.take(16), Modifier)
        if (!d.revoked) {
            if (editing) {
                OutlinedTextField(value = name, onValueChange = { name = it })
                TextButton(onClick = { state.rename(d, name); editing = false }) { Text("Save name") }
            } else {
                Row {
                    TextButton(onClick = { editing = true }) { Text("Rename") }
                    TextButton(onClick = { state.revoke(d) }) { Text("Revoke", color = Color(0xFFF85149)) }
                }
            }
        }
    }
}

@Composable
private fun QrCode(text: String) {
    val matrix = remember(text) { QRCodeWriter().encode(text, BarcodeFormat.QR_CODE, 0, 0) }
    Canvas(Modifier.size(200.dp)) {
        val n = matrix.width + 8
        val cell = size.minDimension / n
        drawRect(Color.White, Offset.Zero, Size(size.minDimension, size.minDimension))
        for (y in 0 until matrix.height) for (x in 0 until matrix.width) {
            if (matrix.get(x, y)) drawRect(Color.Black, Offset((x + 4) * cell, (y + 4) * cell), Size(cell, cell))
        }
    }
}
