package com.fuckbot.sno

import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothSocket
import java.io.IOException
import java.io.OutputStream

/**
 * Канал Bluetooth Classic (SPP) до HC-06.
 *
 * Отдельный поток каждые [Protocol.SEND_PERIOD_MS] мс отправляет последнюю
 * команду — даже если стик не двигается. Этот «пульс» держит живым failsafe
 * в прошивке: пропал пульс на 600 мс — робот встал.
 */
class BtLink(private val adapter: BluetoothAdapter) {

    enum class State { DISCONNECTED, CONNECTING, CONNECTED, ERROR }

    /** Колбэк состояния. Вызывается из рабочего потока — UI обязан переключиться сам. */
    var onState: ((State, String) -> Unit)? = null

    private val lock = Any()
    private var commandY = 0
    private var commandX = 0

    @Volatile
    private var running = false

    @Volatile
    private var socket: BluetoothSocket? = null

    @Volatile
    private var output: OutputStream? = null

    private var worker: Thread? = null

    val isConnected: Boolean get() = output != null

    /** Новая команда со стика. Можно звать из любого потока. */
    fun setCommand(y: Int, x: Int) {
        synchronized(lock) {
            commandY = y
            commandX = x
        }
    }

    fun connect(device: BluetoothDevice) {
        if (running) return
        setCommand(0, 0)
        running = true

        val name = deviceName(device)
        emit(State.CONNECTING, name)

        val thread = Thread({
            try {
                try {
                    adapter.cancelDiscovery()
                } catch (ignored: SecurityException) {
                    // нет разрешения — не критично, discovery и так обычно выключен
                }
                val s = device.createRfcommSocketToServiceRecord(Protocol.SPP_UUID)
                socket = s
                s.connect()
                output = s.outputStream
                emit(State.CONNECTED, name)
                sendLoop()
            } catch (e: SecurityException) {
                emit(State.ERROR, "Нет разрешения на Bluetooth")
            } catch (e: IOException) {
                emit(State.ERROR, "Не удалось подключиться к $name")
            } finally {
                cleanup()
            }
        }, "bt-link")

        thread.isDaemon = true
        worker = thread
        thread.start()
    }

    fun disconnect() {
        val thread = worker
        running = false
        thread?.interrupt()
        if (thread != null && thread !== Thread.currentThread()) {
            try {
                thread.join(500)
            } catch (e: InterruptedException) {
                Thread.currentThread().interrupt()
            }
        }
        worker = null
        cleanup()
        emit(State.DISCONNECTED, "")
    }

    private fun sendLoop() {
        val out = output ?: return
        while (running) {
            val (y, x) = synchronized(lock) { commandY to commandX }
            try {
                out.write(Protocol.frame(y, x))
            } catch (e: IOException) {
                if (running) emit(State.ERROR, "Связь потеряна")
                running = false
                return
            }
            try {
                Thread.sleep(Protocol.SEND_PERIOD_MS)
            } catch (e: InterruptedException) {
                running = false
                return
            }
        }
    }

    /** Идемпотентно: отправляет последний «стоп» и закрывает сокет. */
    private fun cleanup() {
        running = false
        val out = output
        output = null
        if (out != null) {
            try {
                out.write(Protocol.frame(0, 0))
                out.flush()
                Thread.sleep(30) // дать кадру уйти в эфир до закрытия сокета
            } catch (e: IOException) {
                // сокет уже мёртв — ничего не поделать
            } catch (e: InterruptedException) {
                Thread.currentThread().interrupt()
            }
        }
        val s = socket
        socket = null
        try {
            s?.close()
        } catch (e: IOException) {
            // игнорируем
        }
    }

    private fun emit(state: State, message: String) {
        onState?.invoke(state, message)
    }

    private fun deviceName(device: BluetoothDevice): String = try {
        device.name ?: device.address
    } catch (e: SecurityException) {
        device.address
    }
}
