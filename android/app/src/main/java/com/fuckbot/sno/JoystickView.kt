package com.fuckbot.sno

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.View
import kotlin.math.hypot
import kotlin.math.min

/**
 * Экранный джойстик.
 *
 * Отдаёт в [Listener] координаты -1..1, где Y > 0 — «вперёд» (стик вверх),
 * X > 0 — «вправо». Ручка возвращается в центр, когда палец отпущен.
 */
class JoystickView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : View(context, attrs, defStyleAttr) {

    fun interface Listener {
        fun onMove(x: Float, y: Float)
    }

    var listener: Listener? = null

    private val basePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.FILL
        color = Color.parseColor("#1B2230")
    }
    private val ringPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = dp(3f)
        color = Color.parseColor("#35405A")
    }
    private val axisPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = dp(1.5f)
        color = Color.parseColor("#2A3346")
    }
    private val handlePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.FILL
        color = Color.parseColor("#38BDF8")
    }
    private val handleRingPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = dp(2f)
        color = Color.parseColor("#BAE6FD")
    }

    private var baseRadius = 0f
    private var handleRadius = 0f
    private var travel = 0f
    private var offsetX = 0f
    private var offsetY = 0f

    private fun dp(value: Float): Float = value * resources.displayMetrics.density

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        baseRadius = min(w, h) / 2f - dp(8f)
        handleRadius = baseRadius * 0.34f
        travel = (baseRadius - handleRadius).coerceAtLeast(1f)
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        if (baseRadius <= 0f) return

        val cx = width / 2f
        val cy = height / 2f

        canvas.drawCircle(cx, cy, baseRadius, basePaint)
        canvas.drawCircle(cx, cy, baseRadius, ringPaint)

        val arm = baseRadius * 0.74f
        canvas.drawLine(cx - arm, cy, cx + arm, cy, axisPaint)
        canvas.drawLine(cx, cy - arm, cx, cy + arm, axisPaint)
        canvas.drawCircle(cx, cy, travel, axisPaint)

        val hx = cx + offsetX
        val hy = cy + offsetY
        canvas.drawCircle(hx, hy, handleRadius, handlePaint)
        canvas.drawCircle(hx, hy, handleRadius, handleRingPaint)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (!isEnabled) return false
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                parent?.requestDisallowInterceptTouchEvent(true)
                moveHandleTo(event.x, event.y)
                return true
            }
            MotionEvent.ACTION_MOVE -> {
                moveHandleTo(event.x, event.y)
                return true
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                reset()
                performClick()
                return true
            }
        }
        return super.onTouchEvent(event)
    }

    override fun performClick(): Boolean {
        super.performClick()
        return true
    }

    /** Вернуть ручку в центр и сообщить нулевую команду. */
    fun reset() {
        offsetX = 0f
        offsetY = 0f
        invalidate()
        notifyMove()
    }

    private fun moveHandleTo(px: Float, py: Float) {
        if (travel <= 0f) return
        var dx = px - width / 2f
        var dy = py - height / 2f
        val len = hypot(dx, dy)
        if (len > travel) {
            dx = dx / len * travel
            dy = dy / len * travel
        }
        offsetX = dx
        offsetY = dy
        invalidate()
        notifyMove()
    }

    private fun notifyMove() {
        val nx = (offsetX / travel).coerceIn(-1f, 1f)
        val ny = (offsetY / travel).coerceIn(-1f, 1f)
        // Экранная ось Y направлена вниз, а «вперёд» — вверх: инвертируем.
        listener?.onMove(nx, -ny)
    }
}
