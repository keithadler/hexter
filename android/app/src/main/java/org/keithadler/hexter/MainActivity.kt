package org.keithadler.hexter

import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.ListView
import android.widget.SeekBar
import android.widget.Spinner
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity

class MainActivity : AppCompatActivity() {
    private lateinit var engine: HexterEngine
    private lateinit var midi: MidiInput
    private lateinit var status: TextView
    private lateinit var programs: ListView
    private val ui = Handler(Looper.getMainLooper())

    private val openBank = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        uri ?: return@registerForActivityResult
        val bytes = contentResolver.openInputStream(uri)?.use { it.readBytes() } ?: return@registerForActivityResult
        val name = uri.lastPathSegment?.substringAfterLast('/') ?: "bank.syx"
        showBank(engine.loadBank(bytes, name), name)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        status = findViewById(R.id.status)
        programs = findViewById(R.id.programs)
        engine = HexterEngine(this)
        if (!engine.start()) { status.text = getString(R.string.no_audio); return }

        val banks = engine.bundledBanks()
        val bankSpinner = findViewById<Spinner>(R.id.banks)
        bankSpinner.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, banks.map { it.removeSuffix(".dx7") })
        bankSpinner.onItemSelectedListener = object : android.widget.AdapterView.OnItemSelectedListener {
            override fun onItemSelected(p: android.widget.AdapterView<*>?, v: android.view.View?, pos: Int, id: Long) {
                showBank(engine.loadBundledBank(banks[pos]), banks[pos])
            }
            override fun onNothingSelected(p: android.widget.AdapterView<*>?) {}
        }
        findViewById<Button>(R.id.open).setOnClickListener { openBank.launch(arrayOf("*/*")) }

        programs.choiceMode = ListView.CHOICE_MODE_SINGLE
        programs.setOnItemClickListener { _, _, pos, _ -> engine.selectProgram(pos) }

        findViewById<SeekBar>(R.id.volume).apply {
            max = 90; progress = 70                                   // -70 .. +20 dB, default 0
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(s: SeekBar?, p: Int, u: Boolean) = engine.setVolume(p - 70f)
                override fun onStartTrackingTouch(s: SeekBar?) {}
                override fun onStopTrackingTouch(s: SeekBar?) {}
            })
        }
        findViewById<KeyboardView>(R.id.keyboard).onNote = { key, on -> if (on) engine.noteOn(key) else engine.noteOff(key) }

        midi = MidiInput(this, engine) { text -> ui.post { findViewById<TextView>(R.id.midiStatus).text = text } }
        midi.start()

        val roms = banks.indexOfFirst { it.startsWith("dx7_roms") }
        if (roms >= 0) bankSpinner.setSelection(roms) else if (banks.isNotEmpty()) bankSpinner.setSelection(0)
        ui.post(meter)
    }

    private fun showBank(count: Int, name: String) {
        if (count <= 0) { Toast.makeText(this, getString(R.string.bank_failed, name), Toast.LENGTH_LONG).show(); return }
        programs.adapter = ArrayAdapter(this, android.R.layout.simple_list_item_single_choice, engine.programNames())
        programs.setItemChecked(0, true)
        engine.selectProgram(0)
        status.text = getString(R.string.bank_loaded, name.removeSuffix(".dx7"), count, engine.sampleRate)
    }

    private val meter = object : Runnable {
        override fun run() {
            val p = engine.peak
            findViewById<TextView>(R.id.level).text = if (p > 0.001f) "▮".repeat((p * 20).toInt().coerceIn(1, 20)) else ""
            ui.postDelayed(this, 100)
        }
    }

    override fun onDestroy() {
        ui.removeCallbacks(meter)
        if (::midi.isInitialized) midi.stop()
        engine.stop()
        super.onDestroy()
    }
}
