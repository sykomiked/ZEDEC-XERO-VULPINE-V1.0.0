// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.core

import android.os.Handler
import android.os.Looper
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.InetSocketAddress
import java.nio.ByteBuffer
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicInteger

/** Devmesh frames over UDP on the local network. Frames are already encrypted and
 *  authenticated by the core; this only carries bytes. Large handshake frames are
 *  split into 1200-byte fragments ("ZF" | msg id u32 | index u16 | count u16 | data).
 *  Frames for an unknown device (and pairing frames, addressed to the zero id) are
 *  broadcast; the address of every device that sends us a frame is learned from
 *  the sender id in the devmesh header (bytes 20..35). Internet reachability is
 *  a separate carrier (Carracho / EHOP, not in this file). */
class LanTransport(private val deliver: (ByteArray) -> Unit) {
    private val port = 47400
    private val socket = DatagramSocket(null).apply { reuseAddress = true; broadcast = true; bind(InetSocketAddress(port)) }
    private val peers = ConcurrentHashMap<String, InetSocketAddress>()
    private val parts = ConcurrentHashMap<String, Array<ByteArray?>>()
    private val nextId = AtomicInteger((System.nanoTime() and 0x7fffffff).toInt())
    private val main = Handler(Looper.getMainLooper())
    @Volatile private var running = true

    fun start() {
        Thread({
            val buf = ByteArray(1500)
            while (running) {
                val p = DatagramPacket(buf, buf.size)
                try { socket.receive(p) } catch (e: Exception) { continue }
                onPacket(p.data.copyOf(p.length), InetSocketAddress(p.address, p.port))
            }
        }, "zxv-lan").apply { isDaemon = true }.start()
    }

    fun stop() { running = false; socket.close() }

    /** Called by the core (main thread). */
    fun send(to: ByteArray, frame: ByteArray): Int {
        val dst = if (to.all { it.toInt() == 0 }) null else peers[hex(to)]
        val addr = dst ?: InetSocketAddress(InetAddress.getByName("255.255.255.255"), port)
        val id = nextId.incrementAndGet()
        val chunk = 1200
        val count = (frame.size + chunk - 1) / chunk
        Thread {
            for (i in 0 until count) {
                val n = minOf(chunk, frame.size - i * chunk)
                val b = ByteBuffer.allocate(10 + n).put('Z'.code.toByte()).put('F'.code.toByte())
                    .putInt(id).putShort(i.toShort()).putShort(count.toShort()).put(frame, i * chunk, n)
                try { socket.send(DatagramPacket(b.array(), b.position(), addr)) } catch (_: Exception) {}
            }
        }.start()
        return 0
    }

    private fun onPacket(p: ByteArray, from: InetSocketAddress) {
        if (p.size < 10 || p[0] != 'Z'.code.toByte() || p[1] != 'F'.code.toByte()) return
        val bb = ByteBuffer.wrap(p)
        bb.position(2)
        val id = bb.int
        val idx = bb.short.toInt() and 0xffff
        val count = bb.short.toInt() and 0xffff
        if (count == 0 || count > 96 || idx >= count) return
        val key = "$from/$id"
        val arr = parts.getOrPut(key) { arrayOfNulls(count) }
        if (arr.size != count) return
        arr[idx] = p.copyOfRange(10, p.size)
        if (arr.any { it == null }) return
        parts.remove(key)
        val frame = arr.fold(ByteArray(0)) { acc, b -> acc + b!! }
        if (frame.size >= 36) peers[hex(frame.copyOfRange(20, 36))] = from
        main.post { deliver(frame) }
        if (parts.size > 64) parts.clear() // bounded: drop stale partial frames
    }

    private fun hex(b: ByteArray) = b.joinToString("") { "%02x".format(it) }
}
