package org.keithadler.hexter

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.View

/** Two octaves to play with fingers when no keyboard is plugged in. Multi-touch, one note per finger. */
class KeyboardView @JvmOverloads constructor(context: Context, attrs: AttributeSet? = null) : View(context, attrs) {
    var onNote: ((key: Int, on: Boolean) -> Unit)? = null
    var lowest = 48                      // C3
    private val octaves = 2
    private val whiteNotes = intArrayOf(0, 2, 4, 5, 7, 9, 11)
    private val blackNotes = intArrayOf(1, 3, 6, 8, 10)
    private val blackAfterWhite = intArrayOf(0, 1, 3, 4, 5)   // black key sits after these white indexes
    private val held = mutableMapOf<Int, Int>()               // pointer id -> key
    private val white = Paint().apply { color = Color.rgb(235, 235, 235) }
    private val black = Paint().apply { color = Color.rgb(30, 30, 30) }
    private val down = Paint().apply { color = Color.rgb(255, 140, 0) }
    private val line = Paint().apply { color = Color.rgb(80, 80, 80); strokeWidth = 2f; style = Paint.Style.STROKE }

    private fun whiteWidth() = width.toFloat() / (7 * octaves)

    override fun onDraw(c: Canvas) {
        val w = whiteWidth(); val h = height.toFloat()
        for (i in 0 until 7 * octaves) {
            val key = lowest + (i / 7) * 12 + whiteNotes[i % 7]
            val r = RectF(i * w, 0f, (i + 1) * w, h)
            c.drawRect(r, if (held.containsValue(key)) down else white)
            c.drawRect(r, line)
        }
        for (o in 0 until octaves) for (k in blackNotes.indices) {
            val key = lowest + o * 12 + blackNotes[k]
            val x = (o * 7 + blackAfterWhite[k] + 1) * w
            val r = RectF(x - w * 0.3f, 0f, x + w * 0.3f, h * 0.6f)
            c.drawRect(r, if (held.containsValue(key)) down else black)
        }
    }

    private fun keyAt(x: Float, y: Float): Int {
        val w = whiteWidth()
        if (y < height * 0.6f) for (o in 0 until octaves) for (k in blackNotes.indices) {
            val cx = (o * 7 + blackAfterWhite[k] + 1) * w
            if (x >= cx - w * 0.3f && x <= cx + w * 0.3f) return lowest + o * 12 + blackNotes[k]
        }
        val i = (x / w).toInt().coerceIn(0, 7 * octaves - 1)
        return lowest + (i / 7) * 12 + whiteNotes[i % 7]
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val idx = e.actionIndex; val key = keyAt(e.getX(idx), e.getY(idx))
                held[e.getPointerId(idx)] = key; onNote?.invoke(key, true)
            }
            MotionEvent.ACTION_MOVE -> for (idx in 0 until e.pointerCount) {
                val id = e.getPointerId(idx); val key = keyAt(e.getX(idx), e.getY(idx))
                val was = held[id]
                if (was != null && was != key) { onNote?.invoke(was, false); held[id] = key; onNote?.invoke(key, true) }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
                val id = e.getPointerId(e.actionIndex)
                held.remove(id)?.let { onNote?.invoke(it, false) }
                if (e.actionMasked == MotionEvent.ACTION_CANCEL) { held.values.forEach { onNote?.invoke(it, false) }; held.clear() }
            }
        }
        invalidate()
        return true
    }
}
