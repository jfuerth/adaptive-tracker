package io.github.jfuerth.adaptivetracker.editor

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

private const val NATURAL_MINOR = 0b0101_1010_1101  // 0, 2, 3, 5, 7, 8, 10
private const val MAJOR_PENTATONIC = 0b0010_1001_0101  // 0, 2, 4, 7, 9

private val C_MAJOR = Key(0, Pitch.MAJOR)
private fun ctx(current: Key = C_MAJOR, transpose: Int = 0) = PitchContext(C_MAJOR, current, transpose)
private fun deg(octave: Int, degree: Int) = octave * 12 + degree

class PitchTest {
    @Test fun scaleMasks() {
        assertEquals(7, Pitch.scaleSize(Pitch.MAJOR))
        assertEquals(5, Pitch.scaleSize(MAJOR_PENTATONIC))
        assertEquals(3, Pitch.scaleNoteSemitones(NATURAL_MINOR, 2))
    }

    @Test fun rootShiftTakesTheShortWay() {
        assertEquals(2, Pitch.rootShift(0, 2))
        assertEquals(-3, Pitch.rootShift(0, 9))
        assertEquals(1, Pitch.rootShift(11, 0))
    }

    // Same expectations as core/tests/test_pitch.cpp.
    @Test fun resolveMatchesTheEngine() {
        assertEquals(60, Pitch.resolve(PitchMode.DEGREE, deg(4, 0), 0, ctx()))
        assertEquals(64, Pitch.resolve(PitchMode.DEGREE, deg(4, 2), 0, ctx()))
        assertEquals(72, Pitch.resolve(PitchMode.DEGREE, deg(4, 7), 0, ctx()))
        assertEquals(66, Pitch.resolve(PitchMode.DEGREE, deg(4, 3), 1, ctx()))
        assertEquals(63, Pitch.resolve(PitchMode.DEGREE, deg(4, 2), 0, ctx(Key(0, NATURAL_MINOR))))
        assertEquals(57, Pitch.resolve(PitchMode.DEGREE, deg(4, 0), 0, ctx(Key(9, NATURAL_MINOR))))
        assertEquals(72, Pitch.resolve(PitchMode.DEGREE, deg(4, 5), 0, ctx(Key(0, MAJOR_PENTATONIC))))
        assertEquals(64 + 2 + 12, Pitch.resolve(PitchMode.TRANSPOSE, 64, 0, ctx(Key(2, NATURAL_MINOR), 12)))
    }

    @Test fun encodeIsTheInverseOfResolve() {
        val keys = listOf(C_MAJOR, Key(0, NATURAL_MINOR), Key(9, NATURAL_MINOR), Key(2, Pitch.MAJOR))
        for (key in keys) {
            val c = ctx(key)
            for (midi in 36..84) {
                for (mode in PitchMode.entries) {
                    val (note, accidental) = Pitch.encode(midi, mode, c) ?: error("no encoding for $midi in $key")
                    assertEquals("$mode $key midi $midi", midi, Pitch.resolve(mode, note, accidental, c))
                }
            }
        }
    }

    @Test fun degreeEncodingPrefersScaleNotes() {
        assertEquals(deg(4, 2) to 0, Pitch.encode(64, PitchMode.DEGREE, ctx()))  // E is degree 3
        assertEquals(deg(4, 3) to 1, Pitch.encode(66, PitchMode.DEGREE, ctx()))  // F# is a sharpened 4th
    }

    @Test fun noteNames() {
        assertEquals("C-4", Pitch.noteName(60))
        assertEquals("F#3", Pitch.noteName(54))
        assertEquals("A-4", Pitch.noteName(69))
    }
}

class ChordTest {
    @Test fun singleNoteKeepsVolumeAndEffect() {
        val existing = CellData(volume = 0x40, effect = 'F'.code, param = 0x80)
        val cell = Editing.chordCell(listOf(62), PitchMode.ABSOLUTE, ctx(), existing)!!
        assertEquals(CellData(62, 0, 0x40, 'F'.code, 0x80), cell)
    }

    @Test fun absoluteChordBecomesSemitoneArpeggio() {
        val cell = Editing.chordCell(listOf(67, 60, 64), PitchMode.ABSOLUTE, ctx(), CellData())!!
        assertEquals(60, cell.note)
        assertEquals('0'.code, cell.effect)
        assertEquals(0x47, cell.param)
    }

    @Test fun degreeChordBecomesScaleStepArpeggio() {
        // C-E-G in C major: root, +2 and +4 scale steps, so it follows key changes.
        val cell = Editing.chordCell(listOf(60, 64, 67), PitchMode.DEGREE, ctx(), CellData())!!
        assertEquals(deg(4, 0), cell.note)
        assertEquals(0x24, cell.param)
    }

    @Test fun twoNoteChordAndOutOfRangeOffsets() {
        assertEquals(0x70, Editing.chordCell(listOf(60, 67), PitchMode.ABSOLUTE, ctx(), CellData())!!.param)
        // An offset over 15 semitones can't be stored and is dropped.
        val wide = Editing.chordCell(listOf(48, 52, 72), PitchMode.ABSOLUTE, ctx(), CellData())!!
        assertEquals(0x40, wide.param)
    }

    @Test fun emptyChord() {
        assertNull(Editing.chordCell(emptyList(), PitchMode.ABSOLUTE, ctx(), CellData()))
    }
}

class HexEntryTest {
    private fun type(start: CellData, from: Field, digits: List<Int>): CellData {
        var cell = start
        var field: Field? = from
        for (d in digits) {
            cell = Editing.typeHex(cell, field!!, d)
            field = field.nextDigit
        }
        return cell
    }

    @Test fun effectGroupIsCommandThenTwoDigits() {
        val cell = type(CellData(), Field.EFFECT, listOf(3, 0, 5))
        assertEquals('3'.code, cell.effect)
        assertEquals(0x05, cell.param)
        assertNull(Field.PARAM_Y.nextDigit)
    }

    @Test fun volumeIsClampedTo7F() {
        assertEquals(0x7F, type(CellData(), Field.VOLUME_HI, listOf(0x9, 0xF)).volume)
        assertEquals(Field.VOLUME_LO, Field.VOLUME_HI.nextDigit)
        assertNull(Field.VOLUME_LO.nextDigit)
    }

    @Test fun eachDigitCanBeWrittenAlone() {
        val start = CellData(volume = 0x40, effect = 'F'.code, param = 0x80)
        assertEquals(0x83, Editing.typeHex(start, Field.PARAM_Y, 0x3).param)
        assertEquals(0x20, Editing.typeHex(start, Field.PARAM_X, 0x2).param)
        assertEquals('F'.code, Editing.typeHex(start, Field.PARAM_X, 0x2).effect)
        assertEquals('8'.code, Editing.typeHex(start, Field.EFFECT, 0x8).effect)
        assertEquals(0x80, Editing.typeHex(start, Field.EFFECT, 0x8).param)
        assertEquals(0x4A, Editing.typeHex(start, Field.VOLUME_LO, 0xA).volume)
        assertEquals(0x05, Editing.typeHex(CellData(), Field.VOLUME_LO, 0x5).volume)
    }

    @Test fun parameterWithoutCommandIsShown() {
        assertEquals(".05", CellFormat.effect(Editing.typeHex(CellData(), Field.PARAM_Y, 5)))
    }

    @Test fun clearFields() {
        val cell = CellData(60, 0, 0x40, 'F'.code, 0x80)
        assertEquals(CellData.NOTE_EMPTY, Editing.clear(cell, Field.NOTE).note)
        assertEquals(CellData.VOLUME_DEFAULT, Editing.clear(cell, Field.VOLUME_LO).volume)
        assertEquals(CellData.EFFECT_NONE, Editing.clear(cell, Field.PARAM_Y).effect)
    }

    @Test fun hexDigitsMapToCommands() {
        assertEquals('B'.code, CellData.effectFromDigit(11))
        assertEquals(15, CellData.digitFromEffect('F'.code))
        assertEquals(-1, CellData.digitFromEffect(CellData.EFFECT_NONE))
    }
}

class DescriptionTest {
    @Test fun describesEffects() {
        assertEquals("305 · Slide to note · 5/16 semitone per tick",
            EffectDescription.describe(CellData(effect = '3'.code, param = 5), PitchMode.ABSOLUTE))
        assertEquals("024 · Arpeggio · note, +2, +4 scale steps",
            EffectDescription.describe(CellData(effect = '0'.code, param = 0x24), PitchMode.DEGREE))
        assertEquals("B01 · Fire event 1 · Pickup",
            EffectDescription.describe(CellData(effect = 'B'.code, param = 1), PitchMode.DEGREE, listOf("Hit", "Pickup")))
        assertTrue(EffectDescription.describe(CellData(effect = 'F'.code, param = 0xFF), PitchMode.ABSOLUTE)
            .endsWith("20.0 kHz"))
        assertEquals("No effect · type a command digit", EffectDescription.describe(CellData(), PitchMode.ABSOLUTE))
        assertEquals("810 · Filter sweep up · 0.94 octave per beat, until stopped",
            EffectDescription.describe(CellData(effect = '8'.code, param = 0x10), PitchMode.ABSOLUTE))
        assertEquals("900 · Filter sweep · 900 stops the sweep",
            EffectDescription.describe(CellData(effect = '9'.code, param = 0), PitchMode.ABSOLUTE))
    }

    @Test fun formatsCells() {
        val c = ctx()
        assertEquals("E-4", CellFormat.note(CellData(note = deg(4, 2)), PitchMode.DEGREE, c))
        assertEquals("E-4", CellFormat.note(CellData(note = 64), PitchMode.ABSOLUTE, c))
        assertEquals("===", CellFormat.note(CellData(note = CellData.NOTE_OFF), PitchMode.ABSOLUTE, c))
        assertEquals("---", CellFormat.note(CellData(), PitchMode.ABSOLUTE, c))
        assertEquals("..", CellFormat.volume(CellData()))
        assertEquals("305", CellFormat.effect(CellData(effect = '3'.code, param = 5)))
    }
}
