package io.github.derek20la.hidefradio

import java.util.Locale

/**
 * Android Auto: what the car's screen lists (your presets) and what a spoken "play ..." means.
 * RadioService is the MediaBrowserService; this holds the parts that need no Android, so the
 * unit tests can check them.
 *
 * A list item's id is "preset/<freq Hz>/<program>", e.g. "preset/95500000/1" = 95.5 HD2.
 */
object CarBrowse {
    const val ROOT = "root"
    private const val PRESET = "preset/"

    fun presetId(freqHz: Int, program: Int) = "$PRESET$freqHz/$program"

    /** "preset/95500000/1" -> (95500000, 1); null if it's not one of ours. */
    fun parseId(id: String): Pair<Int, Int>? {
        if (!id.startsWith(PRESET)) return null
        val parts = id.removePrefix(PRESET).split('/')
        if (parts.size != 2) return null
        val hz = parts[0].toIntOrNull() ?: return null
        val program = parts[1].toIntOrNull() ?: return null
        return if (hz > 0 && program in 0..7) hz to program else null
    }

    /**
     * What "play 95.5", "play 95.5 HD2", "play 1260" or "play KJZZ" asks for: (freq Hz, program).
     * A frequency in the FM band (87.5-108 MHz) or AM band (530-1710 kHz) is tuned even if it isn't
     * a preset; otherwise the first preset whose name, call sign or program name contains the words. Null: neither.
     */
    fun parseSearch(query: String, presets: List<Preset>): Pair<Int, Int>? {
        val q = query.lowercase(Locale.US).trim()
        if (q.isEmpty()) return null
        val hd = Regex("""\bhd\s*-?\s*([1-8])\b""").find(q)
        val program = hd?.groupValues?.get(1)?.toInt()?.minus(1) ?: 0
        val rest = if (hd != null) q.removeRange(hd.range) else q
        val number = Regex("""\b(\d{2,4}(?:[.,]\d{1,2})?)\b""").find(rest)?.groupValues?.get(1)
            ?.replace(',', '.')?.toDoubleOrNull()
        if (number != null) {
            if (number in 87.5..108.0) return Math.round(number * 1_000_000).toInt() to program
            if (number in 530.0..1710.0 && number == Math.floor(number)) return number.toInt() * 1000 to program
        }
        val words = rest.replace(Regex("""\b(on|the|radio|station|fm|am)\b"""), " ").trim()
            .replace(Regex("""\s+"""), " ")
        if (words.isEmpty()) return null
        val found = presets.firstOrNull { p ->
            (hd == null || p.program == program) &&
                listOf(p.title, p.station, p.label, p.name).any { it.lowercase(Locale.US).contains(words) }
        } ?: return null
        return found.freqHz to found.program
    }

    /** The next HD program after [current] among [numbers] (wrapping around); -1 if there's no other. */
    fun nextProgram(current: Int, numbers: List<Int>): Int {
        val sorted = numbers.distinct().sorted()
        if (sorted.size < 2) return -1
        return sorted.firstOrNull { it > current } ?: sorted.first()
    }
}
