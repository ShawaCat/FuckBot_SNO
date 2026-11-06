package com.fuckbot.sno

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.drawable.GradientDrawable
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.view.View
import android.view.WindowManager
import android.widget.Button
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Toast

class MainActivity : Activity(), JoystickView.Listener {

    private lateinit var joystick: JoystickView
    private lateinit var statusText: TextView
    private lateinit var commandText: TextView
    private lateinit var connectButton: Button
    private lateinit var speedBar: SeekBar
    private lateinit var speedLabel: TextView
    private lateinit var statusDot: View

    private val dotDrawable = GradientDrawable()

    private var adapter: BluetoothAdapter? = null
    private var link: BtLink? = null
    private var maxSpeed = 100

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        joystick = findViewById(R.id.joystick)
        statusText = findViewById(R.id.statusText)
        commandText = findViewById(R.id.commandText)
        connectButton = findViewById(R.id.connectButton)
        speedBar = findViewById(R.id.speedBar)
        speedLabel = findViewById(R.id.speedLabel)
        statusDot = findViewById(R.id.statusDot)
        val stopButton: Button = findViewById(R.id.stopButton)

        dotDrawable.shape = GradientDrawable.OVAL
        dotDrawable.setColor(getColor(R.color.state_off))
        statusDot.background = dotDrawable

        joystick.listener = this
        joystick.contentDescription = getString(R.string.joystick_desc)

        connectButton.setOnClickListener { onConnectClicked() }
        stopButton.setOnClickListener { stopNow() }

        speedBar.max = 100
        speedBar.progress = 100
        speedBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar, progress: Int, fromUser: Boolean) {
                maxSpeed = progress.coerceAtLeast(10)
                speedLabel.text = getString(R.string.speed_value, maxSpeed)
            }

            override fun onStartTrackingTouch(seekBar: SeekBar) = Unit
            override fun onStopTrackingTouch(seekBar: SeekBar) = Unit
        })
        speedLabel.text = getString(R.string.speed_value, maxSpeed)

        val manager = getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
        adapter = manager?.adapter
        link = adapter?.let { createLink(it) }
        if (adapter == null) {
            updateStatus(BtLink.State.ERROR, getString(R.string.no_bluetooth))
        } else {
            updateStatus(BtLink.State.DISCONNECTED, "")
        }
        commandText.text = getString(R.string.command_idle)
    }

    // ------------------------------------------------------------------
    //  Жизненный цикл
    // ------------------------------------------------------------------

    override fun onPause() {
        // Робот не должен уехать без присмотра: гасим стик и рвём канал.
        joystick.reset()
        link?.disconnect()
        super.onPause()
    }

    // ------------------------------------------------------------------
    //  Джойстик
    // ------------------------------------------------------------------

    override fun onMove(x: Float, y: Float) {
        val yPercent = Protocol.stickToPercent(y) * maxSpeed / 100
        val xPercent = Protocol.stickToPercent(x) * maxSpeed / 100
        link?.setCommand(yPercent, xPercent)
        commandText.text = getString(R.string.command_value, yPercent, xPercent)
    }

    private fun stopNow() {
        joystick.reset() // вызовет onMove(0, 0)
        link?.setCommand(0, 0)
    }

    // ------------------------------------------------------------------
    //  Подключение
    // ------------------------------------------------------------------

    private fun createLink(bluetoothAdapter: BluetoothAdapter): BtLink {
        val created = BtLink(bluetoothAdapter)
        created.onState = { state, message -> runOnUiThread { onLinkState(state, message) } }
        return created
    }

    private fun onConnectClicked() {
        val current = link
        if (current != null && current.isConnected) {
            current.disconnect()
            return
        }

        val bluetoothAdapter = adapter
        if (bluetoothAdapter == null) {
            toast(getString(R.string.no_bluetooth))
            return
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S && !hasConnectPermission()) {
            requestPermissions(arrayOf(Manifest.permission.BLUETOOTH_CONNECT), REQ_CONNECT)
            return
        }

        if (!bluetoothAdapter.isEnabled) {
            startActivityForResult(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE), REQ_ENABLE)
            return
        }

        showDevicePicker(bluetoothAdapter)
    }

    private fun showDevicePicker(bluetoothAdapter: BluetoothAdapter) {
        val bonded: List<BluetoothDevice> = try {
            bluetoothAdapter.bondedDevices?.toList() ?: emptyList()
        } catch (e: SecurityException) {
            emptyList()
        }

        if (bonded.isEmpty()) {
            AlertDialog.Builder(this)
                .setTitle(R.string.no_devices_title)
                .setMessage(R.string.no_devices_message)
                .setPositiveButton(R.string.open_bt_settings) { _, _ -> openBluetoothSettings() }
                .setNegativeButton(android.R.string.cancel, null)
                .show()
            return
        }

        val names = bonded.map { deviceTitle(it) }.toTypedArray()
        AlertDialog.Builder(this)
            .setTitle(R.string.pick_device)
            .setItems(names) { _, which -> link?.connect(bonded[which]) }
            .setNeutralButton(R.string.open_bt_settings) { _, _ -> openBluetoothSettings() }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun deviceTitle(device: BluetoothDevice): String {
        val name = try {
            device.name
        } catch (e: SecurityException) {
            null
        }
        return (name ?: getString(R.string.unknown_device)) + "\n" + device.address
    }

    private fun openBluetoothSettings() {
        try {
            startActivity(Intent(Settings.ACTION_BLUETOOTH_SETTINGS))
        } catch (e: Exception) {
            toast(getString(R.string.no_bluetooth))
        }
    }

    private fun hasConnectPermission(): Boolean =
        Build.VERSION.SDK_INT < Build.VERSION_CODES.S ||
            checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode != REQ_CONNECT) return
        val granted = grantResults.isNotEmpty() && grantResults[0] == PackageManager.PERMISSION_GRANTED
        if (granted) {
            onConnectClicked()
        } else {
            toast(getString(R.string.need_permission))
        }
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == REQ_ENABLE && resultCode == RESULT_OK) {
            onConnectClicked()
        }
    }

    // ------------------------------------------------------------------
    //  Индикация
    // ------------------------------------------------------------------

    private fun onLinkState(state: BtLink.State, message: String) {
        updateStatus(state, message)

        when (state) {
            BtLink.State.CONNECTED -> connectButton.text = getString(R.string.disconnect)
            BtLink.State.DISCONNECTED -> connectButton.text = getString(R.string.connect)
            BtLink.State.ERROR -> {
                connectButton.text = getString(R.string.connect)
                if (message.isNotEmpty()) toast(message)
            }
            else -> Unit
        }
    }

    private fun updateStatus(state: BtLink.State, message: String) {
        val (text, color) = when (state) {
            BtLink.State.DISCONNECTED ->
                getString(R.string.status_disconnected) to getColor(R.color.state_off)

            BtLink.State.CONNECTING ->
                getString(R.string.status_connecting, message) to getColor(R.color.state_wait)

            BtLink.State.CONNECTED ->
                getString(R.string.status_connected, message) to getColor(R.color.state_on)

            BtLink.State.ERROR ->
                (if (message.isEmpty()) getString(R.string.status_error) else message) to
                    getColor(R.color.state_err)
        }
        dotDrawable.setColor(color)
        statusText.text = text
    }

    private fun toast(text: String) {
        Toast.makeText(this, text, Toast.LENGTH_SHORT).show()
    }

    private companion object {
        const val REQ_ENABLE = 1
        const val REQ_CONNECT = 2
    }
}
