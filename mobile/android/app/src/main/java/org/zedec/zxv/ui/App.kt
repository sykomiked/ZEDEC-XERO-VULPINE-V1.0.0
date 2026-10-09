// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationBarItemDefaults
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import org.zedec.zxv.model.AppState
import org.zedec.zxv.model.Tab
import org.zedec.zxv.model.Zxv

private val glyphs = mapOf(
    Tab.Assistant to "✦", Tab.Feed to "☰", Tab.Calls to "◉", Tab.Wallet to "◈", Tab.Network to "⌘",
)

@Composable
fun ZxvApp(state: AppState) {
    ZxvTheme {
        Scaffold(
            containerColor = Zc.Bg,
            topBar = { StatusStrip(state) },
            bottomBar = {
                NavigationBar(containerColor = Zc.Surface) {
                    Tab.entries.forEach { t ->
                        NavigationBarItem(
                            selected = state.tab == t,
                            onClick = { state.tab = t },
                            icon = { Text(glyphs[t] ?: "•", fontSize = 18.sp) },
                            label = { Text(t.label) },
                            colors = NavigationBarItemDefaults.colors(
                                selectedTextColor = Zc.Gold, selectedIconColor = Zc.Gold,
                                indicatorColor = Zc.Raised, unselectedTextColor = Zc.Muted,
                                unselectedIconColor = Zc.Muted,
                            ),
                        )
                    }
                }
            },
        ) { pad ->
            Box(Modifier.padding(pad).fillMaxSize().background(Zc.Bg).padding(horizontal = 16.dp)) {
                when (state.tab) {
                    Tab.Assistant -> AssistantScreen(state)
                    Tab.Feed -> FeedScreen()
                    Tab.Calls -> CallsScreen(state)
                    Tab.Wallet -> WalletScreen(state)
                    Tab.Network -> NetworkScreen(state)
                }
            }
        }
        SasDialog(state)
        MoneyDialog(state)
    }
}

@Composable
private fun StatusStrip(state: AppState) {
    Row(
        Modifier.fillMaxWidth().background(Zc.Surface).padding(horizontal = 16.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text("ZXV", fontWeight = FontWeight.Bold, color = Zc.Gold)
        Spacer(Modifier.width(10.dp))
        Box(Modifier.size(8.dp).background(if (state.homeUp) Zc.Green else Zc.Muted, CircleShape))
        Spacer(Modifier.width(6.dp))
        val net = when (state.netMode) {
            Zxv.NET_OFFLINE -> "Offline"
            Zxv.NET_LAN -> "Local network"
            Zxv.NET_METERED -> "Mobile data"
            else -> "Online"
        }
        Muted("$net · post-quantum sessions · " + if (state.homeUp) "home node connected" else "this phone only")
    }
}

@Composable
private fun SasDialog(state: AppState) {
    val s = state.sas ?: return
    AlertDialog(
        onDismissRequest = {},
        title = { Text("Do these numbers match?") },
        text = {
            Column {
                Text(s.digits, fontFamily = Zc.Mono, fontSize = 34.sp, color = Zc.GoldSoft)
                Spacer(Modifier.size(8.dp))
                Muted("The other device shows a number too. Only pair if both are the same. " +
                    "If they differ, someone may be in the middle.")
            }
        },
        confirmButton = { TextButton(onClick = { state.answerSas(true) }) { Text("They match") } },
        dismissButton = { TextButton(onClick = { state.answerSas(false) }) { Text("They differ") } },
    )
}

@Composable
private fun MoneyDialog(state: AppState) {
    val m = state.money ?: return
    AlertDialog(
        onDismissRequest = {},
        title = { Text("Confirm on this phone") },
        text = {
            Column {
                Text("${Zxv.vfv(m.amountMinor)} VFV", fontFamily = Zc.Mono, fontSize = 28.sp, color = Zc.GoldSoft)
                Muted("Rail ${Zxv.railName(m.rail)} · network tithe (φ%) ${Zxv.vfv(state.core.tithe(m.amountMinor))} VFV")
                Spacer(Modifier.size(6.dp))
                Text(m.memo, color = Zc.Text)
                Spacer(Modifier.size(6.dp))
                Muted("Your home computer asked for this. Nothing moves until you approve here.")
            }
        },
        confirmButton = { TextButton(onClick = { state.answerMoney(true) }) { Text("Approve") } },
        dismissButton = { TextButton(onClick = { state.answerMoney(false) }) { Text("Decline") } },
    )
}
