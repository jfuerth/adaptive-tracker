package io.github.jfuerth.adaptivetracker.editor

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class ArrangementTest {
    // Pattern 0: 16 rows at speed 6 (one bar of 4/4). Pattern 1: 3 rows at
    // speed 24 (three beats). Pattern 2: 8 rows at speed 3 (one beat).
    private val shapes = mapOf(
        0 to PatternInfo(0, 16, 6, 2, PitchMode.ABSOLUTE, 24),
        1 to PatternInfo(1, 3, 24, 1, PitchMode.ABSOLUTE, 24),
        2 to PatternInfo(1, 8, 3, 1, PitchMode.ABSOLUTE, 24),
    )
    private val arrangement = Arrangement.build(
        sequences = listOf(listOf(0), listOf(1, 2), emptyList()),
        columns = listOf(2, 1, 1),
        info = { shapes[it] },
        ticksPerBeat = 24,
        beatsPerBar = 4,
    )

    @Test fun lanesLoopTheirOwnSequence() {
        val drums = arrangement.lanes[0]
        assertEquals(96, drums.length)
        val bass = arrangement.lanes[1]
        assertEquals(72 + 24, bass.length)
        assertEquals(Arrangement.Entry(2, 72, 8, 3), bass.entries[1])
        // 100 ticks in: the bass is 4 ticks into its second time round.
        val placed = bass.at(100)!!
        assertEquals(1, placed.entry.pattern)
        assertEquals(96L, placed.start)
        assertEquals(0, placed.rowAt(100))
        assertEquals(2, bass.at(96L + 72 + 7)!!.rowAt(96L + 72 + 7))  // pattern 2, row 2 (3 ticks a row)
        assertNull(arrangement.lanes[2].at(0))  // nothing in the sequence
        assertNull(bass.at(-1))
    }

    @Test fun instancesBetweenTwoTimes() {
        val bass = arrangement.lanes[1]
        val seen = bass.between(60, 200).map { it.entry.pattern to it.start }.toList()
        assertEquals(listOf(1 to 0L, 2 to 72L, 1 to 96L, 2 to 168L, 1 to 192L), seen)
        assertEquals(emptyList<Any>(), arrangement.lanes[2].between(0, 1000).toList())
    }

    @Test fun findsAPatternsNextInstance() {
        val bass = arrangement.lanes[1]
        assertEquals(72L, bass.firstOf(2)?.start)
        assertEquals(168L, bass.firstOf(2, 100)?.start)
        assertEquals(72L, bass.firstOf(2, 80)?.start)  // still playing at 80
        assertNull(bass.firstOf(0))
        assertEquals(96L + 9 * 24, bass.firstOf(1, 100)?.rowStart(9))
    }

    @Test fun barsFromTheSection() {
        assertEquals(96, arrangement.barTicks)
    }
}
