// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.model

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

enum class Tab(val label: String) {
    Assistant("Assistant"), Feed("Feed"), Calls("Calls"), Wallet("Wallet"), Network("Devices")
}

data class ChatMessage(val fromUser: Boolean, val text: String, val where: String = "")

data class SasPrompt(val peer: ByteArray, val sas: Int) {
    val digits: String get() = sas.toString().padStart(6, '0').let { it.substring(0, 3) + " " + it.substring(3) }
    override fun equals(other: Any?) = other is SasPrompt && other.peer.contentEquals(peer) && other.sas == sas
    override fun hashCode() = sas
}

data class MoneyPrompt(val amountMinor: Long, val rail: Int, val memo: String)

/** UI state over the core. Call every method on the main thread; CoreApi
 *  implementations deliver listener callbacks there. */
class AppState(val core: CoreApi, private val clock: () -> Long) : CoreListener {
    var tab by mutableStateOf(Tab.Assistant)
    val messages = mutableStateListOf<ChatMessage>()
    var devices by mutableStateOf(listOf<Device>())
    var sas by mutableStateOf<SasPrompt?>(null)
    var invitation by mutableStateOf<Invitation?>(null)
    var money by mutableStateOf<MoneyPrompt?>(null)
    var netMode by mutableStateOf(Zxv.NET_UNMETERED)
    var homeUp by mutableStateOf(false)
    var notice by mutableStateOf("")
    var localModels by mutableStateOf(listOf<String>())
    var giveBasisPoints by mutableStateOf(162) // contribution slider, 1/100 percent
    private val pending = HashMap<Int, Int>() // request id -> message index
    private val pendingPrompt = HashMap<Int, String>()

    init {
        core.listener = this
        refresh()
    }

    fun refresh() {
        devices = core.devices()
        localModels = core.localModels()
    }

    fun setNetwork(mode: Int, ramMb: Int, compute: Int, battery: Int, charging: Boolean) {
        netMode = mode
        core.setCaps(ramMb, compute, battery, charging, mode, 0, clock())
        localModels = core.localModels()
    }

    fun routeLabel(): String {
        val r = core.route(Zxv.REQ_PROMPT, Zxv.MODEL_LARGE, clock())
        return when (r.route) {
            Zxv.ROUTE_REMOTE -> "Home node · large model"
            Zxv.ROUTE_LOCAL -> "On this device · ${localModels.firstOrNull() ?: "local model"}"
            Zxv.ROUTE_LOCAL_DEGRADED -> "Home node unreachable · small model on this phone"
            Zxv.ROUTE_NEED_CAPACITY -> "No model here · buy capacity on the network"
            else -> "Offline · no model fits this device"
        }
    }

    fun send(text: String) {
        if (text.isBlank()) return
        messages.add(ChatMessage(true, text))
        val now = clock()
        val r = core.route(Zxv.REQ_PROMPT, Zxv.MODEL_LARGE, now)
        if (r.route == Zxv.ROUTE_REMOTE) {
            val id = core.prompt(r.target, text, now)
            if (id > 0) {
                messages.add(ChatMessage(false, "", "Home node"))
                pending[id] = messages.lastIndex
                pendingPrompt[id] = text
                return
            }
        }
        runLocally(text, r.route)
    }

    private fun runLocally(text: String, route: Int) {
        val where = when (route) {
            Zxv.ROUTE_LOCAL, Zxv.ROUTE_LOCAL_DEGRADED -> "This phone · ${localModels.firstOrNull() ?: "small model"}"
            Zxv.ROUTE_NEED_CAPACITY -> "Needs capacity"
            else -> "Unavailable"
        }
        val body = when (route) {
            Zxv.ROUTE_LOCAL, Zxv.ROUTE_LOCAL_DEGRADED ->
                "On-device inference is not wired into this build yet; the prompt \"${text.take(60)}\" " +
                    "would run on ${localModels.firstOrNull() ?: "the small local model"}."
            Zxv.ROUTE_NEED_CAPACITY -> "No model fits this device and the home node is unreachable. " +
                "You can buy inference capacity on the network (Devices > Buy capacity)."
            else -> "Offline and no model fits this device."
        }
        messages.add(ChatMessage(false, body, where))
    }

    fun startInvite(forHome: Boolean) {
        val role = if (forHome) Zxv.ROLE_HOME else Zxv.ROLE_THIN
        val flags = if (forHome) Zxv.FLAG_ADMIN else (Zxv.FLAG_HELD or Zxv.FLAG_ADMIN)
        invitation = core.invite(role, flags, clock())
        if (invitation == null) notice = "Only an admin device of this network can invite."
    }

    fun joinWithCode(code: String) {
        val st = core.joinCode(code, clock())
        notice = if (st == 0) "Looking for the device showing that code…" else "That code is not valid ($st)."
    }

    fun joinWithQr(text: String) {
        val st = core.joinQr(text, clock())
        notice = if (st == 0) "Connecting to the inviting device…" else "That QR code is not valid ($st)."
    }

    fun answerSas(match: Boolean) {
        val s = sas ?: return
        core.confirmSas(s.peer, match, clock())
        sas = null
        invitation = null
    }

    fun answerMoney(approve: Boolean) {
        core.moneyAnswer(approve, clock())
        money = null
    }

    fun revoke(d: Device) {
        val st = core.revoke(d.id, clock())
        notice = if (st == 0) "${d.name} can no longer connect." else "Only an admin device can revoke ($st)."
        refresh()
    }

    fun rename(d: Device, name: String) {
        core.rename(d.id, name, clock())
        refresh()
    }

    // ---- CoreListener ----
    override fun onEvent(kind: Int, peer: ByteArray, sas: Int, value: Long) {
        when (kind) {
            Zxv.EV_SAS -> this.sas = SasPrompt(peer, sas)
            Zxv.EV_PAIRED, Zxv.EV_ROSTER, Zxv.EV_REVOKED -> refresh()
            Zxv.EV_SELF_REVOKED -> { notice = "This device was removed from your network."; refresh() }
            Zxv.EV_PEER_UP -> homeUp = true
            Zxv.EV_PEER_DOWN -> homeUp = false
            Zxv.EV_PAIR_FAILED -> notice = "Pairing cancelled: the numbers did not match."
            Zxv.EV_REQ_FALLBACK -> {
                val id = value.toInt()
                val idx = pending.remove(id)
                val text = pendingPrompt.remove(id)
                if (idx != null && text != null) {
                    messages[idx] = messages[idx].copy(where = "Home node unreachable")
                    runLocally(text, Zxv.ROUTE_LOCAL_DEGRADED)
                }
            }
        }
    }

    override fun onReply(reqId: Int, text: String, final: Boolean) {
        val idx = pending[reqId] ?: return
        messages[idx] = messages[idx].copy(text = messages[idx].text + text)
        if (final) {
            pending.remove(reqId)
            pendingPrompt.remove(reqId)
        }
    }

    override fun onMoney(amountMinor: Long, rail: Int, memo: String) {
        money = MoneyPrompt(amountMinor, rail, memo)
    }
}
