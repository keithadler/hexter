package org.keithadler.hexter

import android.content.Context
import android.media.midi.MidiDevice
import android.media.midi.MidiDeviceInfo
import android.media.midi.MidiManager
import android.media.midi.MidiOutputPort
import android.media.midi.MidiReceiver
import android.os.Handler
import android.os.Looper

/**
 * Opens every MIDI device Android knows about and feeds what they send to the engine. A device's
 * "output port" is the one that sends to us. USB devices appear as soon as they are plugged in;
 * the callback picks them up. Sysex arrives split across packets and is reassembled here so the
 * engine sees whole F0..F7 messages, which is what a DX7 bank dump needs.
 */
class MidiInput(context: Context, private val engine: HexterEngine, private val onChange: (String) -> Unit) {
    private val manager = context.getSystemService(Context.MIDI_SERVICE) as MidiManager
    private val handler = Handler(Looper.getMainLooper())
    private val open = mutableMapOf<String, Pair<MidiDevice, List<MidiOutputPort>>>()

    private val callback = object : MidiManager.DeviceCallback() {
        override fun onDeviceAdded(info: MidiDeviceInfo) = openDevice(info)
        override fun onDeviceRemoved(info: MidiDeviceInfo) { closeDevice(key(info)); report() }
    }

    fun start() {
        manager.registerDeviceCallback(callback, handler)
        manager.devices.forEach { openDevice(it) }
        report()
    }

    fun stop() {
        manager.unregisterDeviceCallback(callback)
        open.keys.toList().forEach { closeDevice(it) }
    }

    private fun key(info: MidiDeviceInfo) = info.id.toString()
    private fun name(info: MidiDeviceInfo): String =
        info.properties.getString(MidiDeviceInfo.PROPERTY_NAME) ?: info.properties.getString(MidiDeviceInfo.PROPERTY_PRODUCT) ?: "MIDI device ${info.id}"

    private fun openDevice(info: MidiDeviceInfo) {
        if (info.outputPortCount == 0 || open.containsKey(key(info))) return
        manager.openDevice(info, { device ->
            if (device == null) return@openDevice
            val ports = (0 until info.outputPortCount).mapNotNull { i ->
                device.openOutputPort(i)?.also { it.connect(Receiver()) }
            }
            open[key(info)] = device to ports
            report()
        }, handler)
    }

    private fun closeDevice(k: String) {
        open.remove(k)?.let { (device, ports) -> ports.forEach { runCatching { it.close() } }; runCatching { device.close() } }
    }

    private fun report() {
        val names = manager.devices.filter { open.containsKey(key(it)) }.map { name(it) }
        onChange(if (names.isEmpty()) "No MIDI input. Plug in a keyboard, or play the one below." else "MIDI in: " + names.joinToString(", "))
    }

    /** Reassembles sysex and hands complete messages to the engine. */
    private inner class Receiver : MidiReceiver() {
        private var sysex: java.io.ByteArrayOutputStream? = null
        override fun onSend(msg: ByteArray, offset: Int, count: Int, timestamp: Long) {
            var i = offset
            val end = offset + count
            while (i < end) {
                val b = msg[i].toInt() and 0xFF
                val sx = sysex
                if (sx != null) {
                    sx.write(b)
                    if (b == 0xF7) { engine.midi(sx.toByteArray()); sysex = null }
                    i++
                    continue
                }
                when {
                    b == 0xF0 -> { sysex = java.io.ByteArrayOutputStream().also { it.write(b) }; i++ }
                    b >= 0xF8 -> i++                                   // realtime: clock, active sensing
                    b >= 0x80 -> {
                        val len = when (b and 0xF0) { 0xC0, 0xD0 -> 2; else -> 3 }
                        if (i + len <= end) engine.midi(msg, i, len)
                        i += len
                    }
                    else -> i++                                        // running status is not used by the FM-1 or most keyboards
                }
            }
        }
    }
}
