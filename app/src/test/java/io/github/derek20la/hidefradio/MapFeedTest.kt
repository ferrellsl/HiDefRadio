package io.github.derek20la.hidefradio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import java.time.Instant

class MapFeedTest {

    private fun utc(text: String) = Instant.parse(text).toEpochMilli()

    @Test
    fun timeInTrafficTileName() {
        assertEquals(utc("2026-10-31T04:14:00Z"), MapFeed.timeInName("TMT_02qa5k_1_1_20261031_0414_034f.png"))
    }

    @Test
    fun timeInRadarName() {
        assertEquals(utc("2026-06-22T22:45:00Z"), MapFeed.timeInName("DWRO_02qris_rev06_20260622_2245_04c8.png"))
    }

    @Test
    fun namesWithoutATime() {
        assertNull(MapFeed.timeInName("trafficMap_1_2_rdhs.png"))
        assertNull(MapFeed.timeInName("DWRI_02qa5k_rev06_0711.txt"))
        assertNull(MapFeed.timeInName("TMT_02qa5k_1_1_20261399_2575_034f.png"))
    }
}
