// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Button
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import org.zedec.zxv.model.AppState
import org.zedec.zxv.model.Zxv

@Composable
fun CallsScreen(state: AppState) {
    Column {
        ScreenTitle("Calls and streams")
        Panel(title = "Start something") {
            Row {
                Button(onClick = {}) { Text("Start a call") }
                Spacer(Modifier.width(8.dp))
                OutlinedButton(onClick = {}) { Text("Go live") }
            }
            Muted("Calls and live streams use kernel/src/call and kernel/src/stream; they are not bridged to the phone in this build.")
        }
        Panel(title = "On mobile data") {
            Muted(
                if (state.netMode == Zxv.NET_METERED) "You are on a metered link: video re-serving is off and calls use the lowest bitrate."
                else "On Wi-Fi this phone re-serves stream pieces to nearby viewers; on mobile data it does not."
            )
        }
        Panel(title = "Live stream: Guild build night") {
            Row {
                Column(Modifier.weight(1f)) { Muted("Viewers"); MonoText("86") }
                Column(Modifier.weight(1f)) { Muted("Your rate"); MonoText("7.9 Mb/s") }
                Column(Modifier.weight(1f)) { Muted("You re-serve"); MonoText("2.1 Mb/s") }
            }
            Muted("Sample figures. Pieces come from many viewers at once: more viewers, more sources.")
        }
    }
}
