package io.github.derek20la.hidefradio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class CarBrowseTest {
    private val presets = listOf(
        Preset(95_500_000, 0, station = "KYOT", label = "Mix 95.5"),
        Preset(91_500_000, 1, station = "KJZZ", label = "Classical"),
    )

    @Test fun ids() {
        assertEquals("preset/95500000/1", CarBrowse.presetId(95_500_000, 1))
        assertEquals(95_500_000 to 1, CarBrowse.parseId("preset/95500000/1"))
        assertNull(CarBrowse.parseId("preset/x/1"))
        assertNull(CarBrowse.parseId("other"))
    }

    @Test fun frequencies() {
        assertEquals(95_500_000 to 0, CarBrowse.parseSearch("95.5", presets))
        assertEquals(95_500_000 to 1, CarBrowse.parseSearch("95.5 HD2", presets))
        assertEquals(102_500_000 to 2, CarBrowse.parseSearch("102.5 hd 3", presets))
        assertEquals(1_260_000 to 0, CarBrowse.parseSearch("1260 AM", presets))
        assertNull(CarBrowse.parseSearch("5000", presets))
    }

    @Test fun names() {
        assertEquals(91_500_000 to 1, CarBrowse.parseSearch("kjzz", presets))
        assertEquals(95_500_000 to 0, CarBrowse.parseSearch("Mix", presets))
        assertNull(CarBrowse.parseSearch("nothing like it", presets))
        assertNull(CarBrowse.parseSearch("", presets))
    }

    @Test fun nextHd() {
        assertEquals(1, CarBrowse.nextProgram(0, listOf(0, 1, 2)))
        assertEquals(0, CarBrowse.nextProgram(2, listOf(0, 1, 2)))
        assertEquals(-1, CarBrowse.nextProgram(0, listOf(0)))
    }
}
