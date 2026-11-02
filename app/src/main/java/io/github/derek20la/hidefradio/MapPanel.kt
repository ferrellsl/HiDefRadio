package io.github.derek20la.hidefradio

import android.app.Dialog
import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Paint
import android.os.Handler
import android.os.Looper
import android.text.format.DateFormat
import android.util.AttributeSet
import android.view.GestureDetector
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.WindowManager
import android.widget.Button
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.TextView
import java.util.Date

/**
 * The traffic and weather maps (MapFeed), under the signal line while the signal details are
 * closed: two tabs, the map, and for the radar a slider through the frames and a play button.
 * The radar plays by itself once there are two frames, until someone pauses it or uses the slider.
 * A double tap on the map fills the screen, and another comes back: a tablet in a car makes a good
 * radio, and there the map should be big, the buttons easy to hit and the screen stay on.
 * Each map has its date and time on the picture, so it can't be taken for a newer one.
 */
class MapPanel @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    private val fullScreen: Boolean = false,
) : LinearLayout(context, attrs) {

    private val trafficTab = tabButton(R.string.map_traffic)
    private val weatherTab = tabButton(R.string.map_weather)
    private val note = TextView(context)
    private val image = ImageView(context)
    private val radarRow = LinearLayout(context)
    private val playButton = Button(context, null, 0, com.google.android.material.R.style.Widget_MaterialComponents_Button_TextButton)
    private val slider = SeekBar(context)
    private val handler = Handler(Looper.getMainLooper())

    private var showWeather = false
    private var seen = -1
    private var frame = 0
    private var frameShown: MapFeed.WeatherFrame? = null
    private var trafficShown: Bitmap? = null
    private var playing = false
    private var autoPlay = true
    private var dialog: Dialog? = null
    private var onDoubleTap: () -> Unit = { openFullScreen() }

    private val player = object : Runnable {
        override fun run() {
            val frames = MapFeed.frames
            if (!playing || frames.size < 2 || !isShown) {
                setPlaying(false)
                return
            }
            frame = (frame + 1) % frames.size
            show()
            handler.postDelayed(this, if (frame == frames.size - 1) PLAY_HOLD_MS else PLAY_STEP_MS)
        }
    }

    init {
        orientation = VERTICAL
        val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        showWeather = prefs.getBoolean(KEY_WEATHER, false)
        autoPlay = prefs.getBoolean(KEY_AUTOPLAY, true)

        val tabs = LinearLayout(context).apply { gravity = Gravity.CENTER_VERTICAL }
        tabs.addView(trafficTab)
        tabs.addView(weatherTab)
        addView(tabs)
        trafficTab.setOnClickListener { pick(false) }
        weatherTab.setOnClickListener { pick(true) }

        note.textSize = if (fullScreen) 18f else 12f
        addView(note, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT))

        radarRow.gravity = Gravity.CENTER_VERTICAL
        playButton.text = PLAY
        playButton.setOnClickListener {
            setPlaying(!playing)
            setAutoPlay(playing)
        }
        radarRow.addView(playButton)
        radarRow.addView(slider, LayoutParams(0, LayoutParams.WRAP_CONTENT, 1f))
        addView(radarRow)          // (above the map, which can fill the pane)

        image.adjustViewBounds = !fullScreen
        image.scaleType = ImageView.ScaleType.FIT_CENTER
        addView(image, if (fullScreen) LayoutParams(LayoutParams.MATCH_PARENT, 0, 1f)
            else LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT))
        val taps = GestureDetector(context, object : GestureDetector.SimpleOnGestureListener() {
            override fun onDown(e: MotionEvent) = true
            override fun onDoubleTap(e: MotionEvent): Boolean {
                this@MapPanel.onDoubleTap()
                return true
            }
        })
        image.setOnTouchListener { view, event ->
            if (event.action == MotionEvent.ACTION_UP) view.performClick()
            taps.onTouchEvent(event)
        }
        if (fullScreen) {
            setBackgroundColor(android.graphics.Color.BLACK)
            for (b in listOf(trafficTab, weatherTab, playButton)) b.textSize = 22f
            slider.minimumHeight = (48 * resources.displayMetrics.density).toInt()
        }
        slider.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(bar: SeekBar, progress: Int, fromUser: Boolean) {
                if (!fromUser) return
                setPlaying(false)
                setAutoPlay(false)
                frame = progress
                show()
            }
            override fun onStartTrackingTouch(bar: SeekBar) {}
            override fun onStopTrackingTouch(bar: SeekBar) {}
        })
    }

    private fun openFullScreen() {
        if (dialog != null) return
        val big = MapPanel(context, null, true)
        val screen = Dialog(context, android.R.style.Theme_Black_NoTitleBar_Fullscreen)
        big.onDoubleTap = { screen.dismiss() }
        screen.setContentView(big)
        screen.window?.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        val ticker = object : Runnable {
            override fun run() {
                big.refresh()
                handler.postDelayed(this, 1000)
            }
        }
        screen.setOnDismissListener {
            handler.removeCallbacks(ticker)
            dialog = null
            // (a tab or the play button may have changed there)
            val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            showWeather = prefs.getBoolean(KEY_WEATHER, false)
            autoPlay = prefs.getBoolean(KEY_AUTOPLAY, true)
            seen = -1
            refresh()
        }
        dialog = screen
        setPlaying(false)
        screen.show()
        ticker.run()
    }

    private fun tabButton(text: Int) =
        Button(context, null, 0, com.google.android.material.R.style.Widget_MaterialComponents_Button_TextButton)
            .apply { setText(text) }

    private fun pick(weather: Boolean) {
        showWeather = weather
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putBoolean(KEY_WEATHER, weather).apply()
        setPlaying(false)
        frame = max(0, MapFeed.frames.size - 1)
        show()
    }

    private fun max(a: Int, b: Int) = if (a > b) a else b

    private fun setAutoPlay(on: Boolean) {
        autoPlay = on
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putBoolean(KEY_AUTOPLAY, on).apply()
    }

    private fun setPlaying(on: Boolean) {
        playing = on
        playButton.text = if (on) PAUSE else PLAY
        handler.removeCallbacks(player)
        if (on) handler.postDelayed(player, PLAY_STEP_MS)
    }

    /** Called once a second by the screen; only does something when there's something new. */
    fun refresh() {
        if (seen != MapFeed.generation) {
            val atNewest = frame >= MapFeed.frames.size - 2     // (stays on the newest as frames come in)
            seen = MapFeed.generation
            if (atNewest || frame >= MapFeed.frames.size) frame = max(0, MapFeed.frames.size - 1)
            show()
        }
        if (autoPlay && showWeather && !playing && dialog == null && MapFeed.frames.size > 1) setPlaying(true)
    }

    private fun show() {
        trafficTab.alpha = if (showWeather) 0.5f else 1f
        weatherTab.alpha = if (showWeather) 1f else 0.5f
        if (showWeather) showWeatherFrame() else showTraffic()
    }

    private fun showTraffic() {
        radarRow.visibility = View.GONE
        frameShown = null
        val map = MapFeed.traffic
        if (map !== trafficShown) {
            trafficShown = map
            image.setImageBitmap(map?.let { stamped(it.copy(Bitmap.Config.ARGB_8888, true), MapFeed.trafficTimeMs) })
        }
        image.visibility = if (map == null) View.GONE else View.VISIBLE
        note.visibility = if (map == null) View.VISIBLE else View.GONE
        note.setText(R.string.map_waiting_traffic)
    }

    private fun showWeatherFrame() {
        val frames = MapFeed.frames
        trafficShown = null
        note.visibility = View.VISIBLE
        if (frames.isEmpty()) {
            frameShown = null
            image.setImageBitmap(null)
            image.visibility = View.GONE
            radarRow.visibility = View.GONE
            note.text = context.getString(R.string.map_waiting_weather)
            return
        }
        val current = frames[frame.coerceIn(0, frames.size - 1)]
        if (current !== frameShown) {
            frameShown = current
            val options = BitmapFactory.Options().apply { inMutable = true }
            val picture = BitmapFactory.decodeByteArray(current.jpeg, 0, current.jpeg.size, options)
            image.setImageBitmap(picture?.let { stamped(it, current.timeMs) })
        }
        image.visibility = View.VISIBLE
        radarRow.visibility = if (frames.size > 1) View.VISIBLE else View.GONE
        slider.max = frames.size - 1
        slider.progress = frame
        note.text = context.getString(R.string.map_radar_frame, frame + 1, frames.size)
    }

    /**
     * Writes the date and time in the top left corner of a map, in the phone's own time zone and
     * 12 or 24 hour clock. Sized by the picture, so it grows with the map in full screen. On the
     * picture as it's shown, not in the frames kept: those stay good if the settings change.
     */
    private fun stamped(map: Bitmap, ms: Long): Bitmap {
        val date = Date(ms)
        val text = DateFormat.getMediumDateFormat(context).format(date) + "  " +
            DateFormat.getTimeFormat(context).format(date)
        val paint = Paint(Paint.ANTI_ALIAS_FLAG)
        paint.textSize = map.width / STAMP_PER_WIDTH
        paint.isFakeBoldText = true
        val pad = paint.textSize / 3
        val canvas = Canvas(map)
        val box = Paint()
        box.color = STAMP_BACKGROUND
        canvas.drawRect(0f, 0f, paint.measureText(text) + 2 * pad, paint.descent() - paint.ascent() + 2 * pad, box)
        paint.color = android.graphics.Color.WHITE
        canvas.drawText(text, pad, pad - paint.ascent(), paint)
        return map
    }

    override fun onDetachedFromWindow() {
        setPlaying(false)
        dialog?.dismiss()
        super.onDetachedFromWindow()
    }

    private companion object {
        const val PREFS = "map_panel"
        const val KEY_WEATHER = "weather"
        const val KEY_AUTOPLAY = "autoplay"
        const val PLAY = "▶"
        const val PAUSE = "⏸"
        const val PLAY_STEP_MS = 600L
        const val PLAY_HOLD_MS = 1800L       // a rest on the newest picture before it starts over
        const val STAMP_PER_WIDTH = 26f      // text height: a map's width over this
        const val STAMP_BACKGROUND = -0x50000000    // black, about two thirds solid
    }
}
