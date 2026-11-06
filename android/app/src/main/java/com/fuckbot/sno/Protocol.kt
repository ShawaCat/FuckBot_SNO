package com.fuckbot.sno

import java.util.Locale
import java.util.UUID
import kotlin.math.abs
import kotlin.math.roundToInt

/**
 * Протокол связи с прошивкой FuckBot_SNO (см. firmware/FuckBot_SNO/FuckBot_SNO.ino).
 *
 * Кадр: "<Y>,<X>\n", оба значения в диапазоне -100..100.
 * Y > 0 — вперёд, X > 0 — вправо.
 *
 * Логика вынесена отдельно от Android, чтобы её можно было прогонять тестами
 * на обычной JVM (см. tools/app_logic_test.kt).
 */
object Protocol {

    /** Bluetooth Classic SPP — стандартный UUID последовательного порта (HC-06). */
    val SPP_UUID: UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")

    /** Мёртвая зона стика: всё, что ближе к центру, считаем нулём. */
    const val STICK_DEAD_ZONE = 0.05f

    /** Минимальный интервал отправки кадров, мс (~50 кадров в секунду). */
    const val SEND_PERIOD_MS = 20L

    /**
     * Положение стика -1..1 -> целые проценты -100..100.
     * Мёртвая зона убирает дрожание пальца и «ползущий» робот в покое.
     */
    fun stickToPercent(value: Float): Int {
        val v = value.coerceIn(-1f, 1f)
        if (abs(v) < STICK_DEAD_ZONE) return 0
        return (v * 100f).roundToInt().coerceIn(-100, 100)
    }

    /**
     * Готовый кадр для UART.
     * Locale.US обязательна: в некоторых локалях String.format выдаёт не ASCII-цифры.
     */
    fun frame(yPercent: Int, xPercent: Int): ByteArray {
        val y = yPercent.coerceIn(-100, 100)
        val x = xPercent.coerceIn(-100, 100)
        return String.format(Locale.US, "%d,%d\n", y, x).toByteArray(Charsets.US_ASCII)
    }
}
