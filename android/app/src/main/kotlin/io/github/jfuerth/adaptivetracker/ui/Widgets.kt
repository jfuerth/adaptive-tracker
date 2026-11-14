package io.github.jfuerth.adaptivetracker.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import io.github.jfuerth.adaptivetracker.editor.Pitch

private val BLACK_KEYS = setOf(1, 3, 6, 8, 10)

/**
 * Twelve keys across the width, starting at MIDI note [firstNote] (any note,
 * not only C). Multi-touch: keys sound while held; when the last finger lifts,
 * [onChord] gets the largest set of notes that were held at the same time (so
 * sliding one finger along gives one note).
 *
 * While [sliding] is true when a touch starts, that touch moves the keyboard
 * instead: [onSlide] gets the semitones to move [firstNote] by, one per key
 * width dragged (drag right to bring lower notes in from the left).
 */
@Composable
fun NoteKeyboard(
    firstNote: Int,
    dimNote: (Int) -> Boolean,
    onNoteDown: (Int) -> Unit,
    onNoteUp: (Int) -> Unit,
    onChord: (Set<Int>) -> Unit,
    modifier: Modifier = Modifier,
    sliding: Boolean = false,
    onSlide: (Int) -> Unit = {},
) {
    val colors = MaterialTheme.colorScheme
    val held = remember { mutableStateListOf<Int>() }
    val first by rememberUpdatedState(firstNote)
    val down by rememberUpdatedState(onNoteDown)
    val up by rememberUpdatedState(onNoteUp)
    val chord by rememberUpdatedState(onChord)
    val slideMode by rememberUpdatedState(sliding)
    val slide by rememberUpdatedState(onSlide)

    Row(
        modifier.pointerInput(Unit) {
            awaitPointerEventScope {
                // Notes, not key positions, so a note always ends the way it started.
                var previous = emptySet<Int>()
                var largest = emptySet<Int>()
                var slidingNow = false
                var slideX = 0f
                while (true) {
                    val event = awaitPointerEvent()
                    val pressed = event.changes.filter { it.pressed }
                    event.changes.forEach { it.consume() }
                    if (previous.isEmpty() && !slidingNow && pressed.isNotEmpty() && slideMode) {
                        slidingNow = true
                        slideX = pressed.first().position.x
                    }
                    if (slidingNow) {
                        if (pressed.isEmpty()) {
                            slidingNow = false
                            continue
                        }
                        val keyWidth = size.width / 12f
                        val x = pressed.first().position.x
                        val steps = ((x - slideX) / keyWidth).toInt()
                        if (steps != 0) {
                            slide(-steps)
                            slideX += steps * keyWidth
                        }
                        continue
                    }
                    val now = pressed
                        .map { first + ((it.position.x / size.width) * 12).toInt().coerceIn(0, 11) }
                        .toSet()
                    (now - previous).forEach { down(it) }
                    (previous - now).forEach { up(it) }
                    if (now.size >= largest.size && now.isNotEmpty()) largest = now
                    previous = now
                    held.clear()
                    held.addAll(now)
                    if (now.isEmpty() && largest.isNotEmpty()) {
                        chord(largest)
                        largest = emptySet()
                    }
                }
            }
        },
    ) {
        for (k in 0 until 12) {
            val note = firstNote + k
            val pressed = note in held
            val black = note % 12 in BLACK_KEYS
            val base = if (black) colors.surfaceVariant else colors.surfaceContainerHighest
            Box(
                Modifier
                    .weight(1f)
                    .fillMaxHeight()
                    .padding(1.dp)
                    .clip(RoundedCornerShape(bottomStart = 6.dp, bottomEnd = 6.dp))
                    .background(if (pressed) colors.primary else if (sliding) colors.tertiaryContainer else base)
                    .alpha(if (dimNote(note)) 0.35f else 1f),
                contentAlignment = Alignment.BottomCenter,
            ) {
                // C and the first key say their octave, so the keyboard's place is clear.
                val name = Pitch.KEY_NAMES[note % 12] + if (note % 12 == 0 || k == 0) "${note / 12 - 1}" else ""
                Text(
                    name,
                    style = MaterialTheme.typography.labelSmall,
                    color = if (pressed) colors.onPrimary else colors.onSurface,
                    maxLines = 1,
                    modifier = Modifier.padding(bottom = 6.dp),
                )
            }
        }
    }
}

/** A small button; [onLongClick], if given, runs on a long press instead of [onClick]. */
@OptIn(ExperimentalFoundationApi::class)
@Composable
fun ToolButton(
    text: String,
    onClick: () -> Unit,
    selected: Boolean = false,
    onLongClick: (() -> Unit)? = null,
    horizontalPadding: Dp = 10.dp,
) {
    val colors = MaterialTheme.colorScheme
    Box(
        Modifier
            .height(36.dp)
            .clip(RoundedCornerShape(8.dp))
            .background(if (selected) colors.primary else colors.secondaryContainer)
            .combinedClickable(onClick = onClick, onLongClick = onLongClick)
            .padding(horizontal = horizontalPadding),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text,
            style = MaterialTheme.typography.labelLarge,
            color = if (selected) colors.onPrimary else colors.onSecondaryContainer,
        )
    }
}

/**
 * A modifier key: [onHeld] gets true while a finger is on it and false when
 * it lifts, so other controls can work differently meanwhile.
 */
@Composable
fun HoldButton(text: String, held: Boolean, onHeld: (Boolean) -> Unit, modifier: Modifier = Modifier) {
    val colors = MaterialTheme.colorScheme
    val set by rememberUpdatedState(onHeld)
    Box(
        modifier
            .height(36.dp)
            .clip(RoundedCornerShape(8.dp))
            .background(if (held) colors.tertiary else colors.tertiaryContainer)
            .pointerInput(Unit) {
                awaitEachGesture {
                    awaitFirstDown(requireUnconsumed = false).consume()
                    set(true)
                    try {
                        do {
                            val event = awaitPointerEvent()
                            event.changes.forEach { it.consume() }
                        } while (event.changes.any { it.pressed })
                    } finally {
                        set(false)
                    }
                }
            }
            .padding(horizontal = 14.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text,
            style = MaterialTheme.typography.labelLarge,
            color = if (held) colors.onTertiary else colors.onTertiaryContainer,
        )
    }
}

/** Asks for a name; [onDone] gets it (trimmed, not blank), or null if cancelled. */
@Composable
fun NameDialog(title: String, initial: String, confirm: String = "OK", onDone: (String?) -> Unit) {
    var text by remember { mutableStateOf(initial) }
    AlertDialog(
        onDismissRequest = { onDone(null) },
        title = { Text(title) },
        text = { OutlinedTextField(text, onValueChange = { text = it }, singleLine = true) },
        confirmButton = {
            TextButton(onClick = { onDone(text.trim()) }, enabled = text.isNotBlank()) { Text(confirm) }
        },
        dismissButton = { TextButton(onClick = { onDone(null) }) { Text("Cancel") } },
    )
}
