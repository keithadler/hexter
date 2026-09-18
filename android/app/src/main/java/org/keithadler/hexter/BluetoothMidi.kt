package org.keithadler.hexter

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.pm.PackageManager
import android.media.midi.MidiDevice
import android.media.midi.MidiManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import android.widget.ArrayAdapter
import android.widget.Toast
import androidx.core.content.ContextCompat
import java.util.UUID

/**
 * Finds Bluetooth LE MIDI keyboards and connects them. Once a keyboard is connected, Android
 * registers it as an ordinary MIDI device and [MidiInput] picks it up the same way as a USB one.
 * The MidiDevice that openBluetoothDevice returns is what keeps the Bluetooth link up, so it is
 * held here for as long as the app runs.
 */
class BluetoothMidi(private val activity: Activity, private val ask: (Array<String>) -> Unit) {
    private val manager = activity.getSystemService(Context.MIDI_SERVICE) as MidiManager
    private val handler = Handler(Looper.getMainLooper())
    private val held = mutableListOf<MidiDevice>()
    private var scanner: BluetoothLeScanner? = null
    private var scan: ScanCallback? = null

    /** What Android wants before it lets an app see nearby Bluetooth devices. */
    val permissions: Array<String> =
        if (Build.VERSION.SDK_INT >= 31) arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
        else arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)

    fun show() {
        val adapter = (activity.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter
        if (adapter == null || !activity.packageManager.hasSystemFeature(PackageManager.FEATURE_BLUETOOTH_LE)) {
            toast(R.string.bt_unavailable); return
        }
        val missing = permissions.filter { ContextCompat.checkSelfPermission(activity, it) != PackageManager.PERMISSION_GRANTED }
        if (missing.isNotEmpty()) { ask(missing.toTypedArray()); return }
        if (!adapter.isEnabled) { toast(R.string.bt_off); return }
        val le = adapter.bluetoothLeScanner ?: run { toast(R.string.bt_off); return }

        stopScan()
        val found = mutableListOf<BluetoothDevice>()
        val names = ArrayAdapter<String>(activity, android.R.layout.simple_list_item_1)
        val dialog = AlertDialog.Builder(activity)
            .setTitle(R.string.bt_searching)
            .setAdapter(names) { _, i -> connect(found[i]) }
            .setNegativeButton(android.R.string.cancel, null)
            .create()
        val callback = object : ScanCallback() {
            override fun onScanResult(type: Int, result: ScanResult) {
                val device = result.device
                if (found.any { it.address == device.address }) return
                found += device
                names.add(result.scanRecord?.deviceName ?: runCatching { device.name }.getOrNull() ?: device.address)
                dialog.setTitle(R.string.bt_found)
            }
            override fun onScanFailed(code: Int) { dialog.setTitle(activity.getString(R.string.bt_scan_failed, code)) }
        }
        dialog.setOnDismissListener { stopScan() }
        dialog.show()
        try {
            le.startScan(listOf(ScanFilter.Builder().setServiceUuid(ParcelUuid(MIDI_SERVICE)).build()),
                ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), callback)
            scanner = le
            scan = callback
        } catch (e: SecurityException) {
            dialog.dismiss(); toast(R.string.bt_unavailable)
        }
    }

    private fun connect(device: BluetoothDevice) {
        stopScan()
        manager.openBluetoothDevice(device, { midi ->
            if (midi == null) { toast(R.string.bt_connect_failed); return@openBluetoothDevice }
            held += midi                       // MidiInput sees the new device through MidiManager's callback
        }, handler)
    }

    private fun stopScan() {
        val s = scan ?: return
        runCatching { scanner?.stopScan(s) }
        scan = null
    }

    fun stop() {
        stopScan()
        held.forEach { runCatching { it.close() } }
        held.clear()
    }

    private fun toast(id: Int) = Toast.makeText(activity, id, Toast.LENGTH_LONG).show()

    companion object {
        /** The Bluetooth LE MIDI service, as every BLE MIDI keyboard advertises it. */
        val MIDI_SERVICE: UUID = UUID.fromString("03B80E5A-EDE8-4B33-A751-6CE34EC4C700")
    }
}
