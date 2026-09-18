package org.keithadler.hexter

import android.content.Context

/** The native engine behind the audio stream. One instance per process; start() opens the stream. */
class HexterEngine(private val context: Context) {
    companion object { init { System.loadLibrary("hexter_jni") } }

    private external fun nativeStart(sampleRate: Int): Boolean
    private external fun nativeStop()
    private external fun nativeSampleRate(): Int
    private external fun nativeLoadBank(bytes: ByteArray, hint: String?): Int
    private external fun nativeProgramName(program: Int): String
    private external fun nativeSelectProgram(program: Int)
    private external fun nativeProgram(): Int
    private external fun nativeSetVolume(db: Float)
    private external fun nativeSetPolyphony(voices: Int): Int
    private external fun nativeMidi(bytes: ByteArray, offset: Int, count: Int): Boolean
    private external fun nativePeak(): Float
    private external fun nativeActiveVoices(): Int

    var running = false; private set
    var patchCount = 0; private set
    var bankName = ""; private set

    fun start(): Boolean { running = nativeStart(0); return running }
    fun stop() { if (running) nativeStop(); running = false }
    val sampleRate: Int get() = nativeSampleRate()

    /** The banks shipped inside the app. */
    fun bundledBanks(): List<String> = context.assets.list("banks")?.filter { it.endsWith(".dx7") }?.sorted() ?: emptyList()

    fun loadBundledBank(name: String): Int {
        val bytes = context.assets.open("banks/$name").use { it.readBytes() }
        return loadBank(bytes, name)
    }

    fun loadBank(bytes: ByteArray, hint: String): Int {
        val n = nativeLoadBank(bytes, hint)
        if (n > 0) { patchCount = n; bankName = hint }
        return n
    }

    fun programNames(): List<String> = (0 until patchCount).map { "%3d  %s".format(it + 1, nativeProgramName(it).trimEnd()) }
    fun selectProgram(program: Int) = nativeSelectProgram(program)
    val program: Int get() = nativeProgram()
    fun setVolume(db: Float) = nativeSetVolume(db)
    fun setPolyphony(voices: Int) = nativeSetPolyphony(voices)
    val peak: Float get() = nativePeak()
    val activeVoices: Int get() = nativeActiveVoices()

    fun midi(bytes: ByteArray, offset: Int = 0, count: Int = bytes.size) = nativeMidi(bytes, offset, count)
    fun noteOn(key: Int, velocity: Int = 100) = midi(byteArrayOf(0x90.toByte(), key.toByte(), velocity.toByte()))
    fun noteOff(key: Int) = midi(byteArrayOf(0x80.toByte(), key.toByte(), 0))
    fun allNotesOff() = midi(byteArrayOf(0xB0.toByte(), 123, 0))
}
