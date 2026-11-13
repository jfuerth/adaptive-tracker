package io.github.jfuerth.adaptivetracker.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.unit.dp
import io.github.jfuerth.adaptivetracker.editor.Pitch

private val BLACK_KEYS = setOf(1, 3, 6, 8, 10)

/**
 * Twelve keys across the width, one octave. Multi-touch: keys sound while held;
 * when the last finger lifts, [onChord] gets the largest set of keys that were
 * held at the same time (so sliding one finger along gives one note).
 */
@Composable
fun NoteKeyboard(
    dimKey: (Int) -> Boolean,
    onKeyDown: (Int) -> Unit,
    onKeyUp: (Int) -> Unit,
    onChord: (Set<Int>) -> Unit,
    modifier: Modifier = Modifier,
) {
    val colors = MaterialTheme.colorScheme
    val held = remember { mutableStateListOf<Int>() }
    val down by rememberUpdatedState(onKeyDown)
    val up by rememberUpdatedState(onKeyUp)
    val chord by rememberUpdatedState(onChord)

    Row(
        modifier.pointerInput(Unit) {
            awaitPointerEventScope {
                var previous = emptySet<Int>()
                var largest = emptySet<Int>()
                while (true) {
                    val event = awaitPointerEvent()
                    val now = event.changes
                        .filter { it.pressed }
                        .map { ((it.position.x / size.width) * 12).toInt().coerceIn(0, 11) }
                        .toSet()
                    event.changes.forEach { it.consume() }
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
            val pressed = k in held
            val base = if (k in BLACK_KEYS) colors.surfaceVariant else colors.surfaceContainerHighest
            Box(
                Modifier
                    .weight(1f)
                    .fillMaxHeight()
                    .padding(1.dp)
                    .clip(RoundedCornerShape(bottomStart = 6.dp, bottomEnd = 6.dp))
                    .background(if (pressed) colors.primary else base)
                    .alpha(if (dimKey(k)) 0.35f else 1f),
                contentAlignment = Alignment.BottomCenter,
            ) {
                Text(
                    Pitch.KEY_NAMES[k],
                    style = MaterialTheme.typography.labelSmall,
                    color = if (pressed) colors.onPrimary else colors.onSurface,
                    modifier = Modifier.padding(bottom = 6.dp),
                )
            }
        }
    }
}

@Composable
fun ToolButton(text: String, onClick: () -> Unit, selected: Boolean = false) {
    val colors = MaterialTheme.colorScheme
    Box(
        Modifier
            .height(36.dp)
            .clip(RoundedCornerShape(8.dp))
            .background(if (selected) colors.primary else colors.secondaryContainer)
            .clickable(onClick = onClick)
            .padding(horizontal = 10.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text,
            style = MaterialTheme.typography.labelLarge,
            color = if (selected) colors.onPrimary else colors.onSecondaryContainer,
        )
    }
}
